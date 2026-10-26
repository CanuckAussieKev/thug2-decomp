// Self-tests for the skate module (Mdl::Skate, 0x00564C10-0x005691EF), the skate-the-line goal (0x005691F0-0x0056952F)
// and the state history's newest entries (0x00569680 / 0x005696A0): the original against ours on the same stand-in
// world - a skate module of its own (its skater list real, its managers stand-ins), skaters with recording vtables and
// component buffers, the network manager's players, the game mode - every call out recorded (a generic stand-in per
// callee: its object and arguments, a pointer into the stack recorded as a marker) and answered from the seed.

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "game/core/crc.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/ed/park_editor.h"
#include "game/front/front_end.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/gfx/tod_manager.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_skate_line.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_conn.h"
#include "game/net/player_info.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/input_component.h"
#include "game/obj/movie_manager.h"
#include "game/obj/node_components.h"
#include "game/obj/objman.h"
#include "game/obj/proxim.h"
#include "game/obj/rail_manager.h"
#include "game/obj/shadow_component.h"
#include "game/obj/smtptr.h"
#include "game/obj/suspend_component.h"
#include "game/obj/trick_objects.h"
#include "game/obj/vibration_component.h"
#include "game/obj/emitter.h"
#include "game/pcm/music_api.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/script/preferences.h"
#include "game/sfx/sfx.h"
#include "game/sfx/terrain.h"
#include "game/sio/device.h"
#include "game/sio/inpman.h"
#include "game/skate/career.h"
#include "game/skate/competition.h"
#include "game/skate/game_mode.h"
#include "game/skate/horse.h"
#include "game/skate/profile_manager.h"
#include "game/skate/records.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "game/skate/skate_net.h"
#include "game/skater/end_run.h"
#include "game/skater/skater.h"
#include "game/skater/state_history.h"
#include "game/skater/trick_component.h"
#include "game/sys/module.h"
#include "game/sys/timer.h"
#include "game/sys/viewer.h"
#include "hook/game.h"
#include "hook/goals_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace GoalsTests;
using Mdl::Skate;

uint32_t A(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t g_seed;
uint32_t Next()
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
bool Coin() { return (Next() & 1) != 0; }
uint32_t Pick(uint32_t n) { return Next() % n; }
uint32_t g_calls;
uint32_t g_salt;
uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = (a ^ 0x9E3779B9u) * 0x85EBCA6Bu ^ b * 0xC2B2AE35u ^ g_salt;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    return h ^ (h >> 13);
}

// --- the world ------------------------------------------------------------------------------------------------------

constexpr int kSkaters = 4;
alignas(16) uint8_t g_skateBuffer[sizeof(Skate) + 0x10];
Skate* TheSkate() { return reinterpret_cast<Skate*>(g_skateBuffer); }
alignas(16) uint8_t g_skaters[kSkaters][0x340];
bool g_inList[kSkaters];
void* g_skaterVtable[32];
void* g_objectVtable[32];
alignas(16) uint8_t g_components[kSkaters][8][0x1540]; // per skater: trick, end run, core physics, shadow, input, vibration, balance, other
alignas(16) uint8_t g_physicsFlags[kSkaters][0x40];
alignas(16) uint8_t g_device[kSkaters][0x40];
alignas(16) uint8_t g_deviceInner[kSkaters][0x40];
alignas(16) uint8_t g_unknown264[kSkaters][0x500];
alignas(16) uint8_t g_stateHistory[kSkaters][0x1108];
alignas(16) uint8_t g_objects[6][0x100];  // bail boards, the skater camera, cameras, a movie manager's camera
bool g_objectPresent[6];
alignas(16) uint8_t g_players[4][0x1F0];
bool g_playerPresent[4];
bool g_playerLocal[4];
alignas(16) uint8_t g_conns[3][0x3D20]; // (a connection's size: its latency test read)
int g_connCursor;
std::map<uint32_t, int> g_playerCursors;
alignas(16) uint8_t g_apps[2][0x40];
bool g_clientPresent[2];
alignas(16) uint8_t g_fakeManagers[26][0x80]; // career, rails, movies x2, game flow, goal manager, prefs, competition, horse, script, ...
alignas(16) uint8_t g_records[0x40];
alignas(16) uint8_t g_recordTables[8][0x20];
alignas(16) uint8_t g_cameras[2][0x80];
bool g_cameraPresent[2];
alignas(16) uint8_t g_elements[4][0x180];
alignas(16) uint8_t g_goals[2][0x100];
void* g_goalVtables[2][32];
std::map<uint32_t, uint32_t> g_answers; // a callee's fixed answer (by address)
uint32_t g_mode;
int g_maxPlayers;

uint32_t SkaterIndex(uint32_t object)
{
    for (int i = 0; i < kSkaters; ++i)
        if (object == A(g_skaters[i]))
            return static_cast<uint32_t>(i);
    return 0xFFu;
}
// A stack address (a local of the caller's - not the same in both runs) as a marker.
uint32_t Where(const Regs& r, uint32_t value)
{
    return value - (r.esp - 0x1000u) < 0x9000u ? 0x57AC : value;
}

// Our functions handed to a callee (callbacks) as the original's addresses.
std::map<uint32_t, uint32_t> g_aliases;

// A generic recording stand-in: the callee's address, its object and its arguments; the answer a custom one, else
// the fixed one, else 0 / 1 from the seed.
std::map<uint32_t, std::function<uint32_t(Regs&)>> g_custom;
void Generic(StandIns& s, uint32_t address, uint32_t ours, uint16_t pop, bool thiscall, int words)
{
    std::vector<uint32_t> addresses = {address};
    if (ours)
        addresses.push_back(ours);
    for (uint32_t at : addresses) {
        s.At({at}, pop, [address, thiscall, words](Regs& r) {
            ++g_calls;
            Record({address & 0xFFFFF, thiscall ? Where(r, r.ecx) : 0u});
            for (int i = 0; i < words; ++i) {
                uint32_t raw = r.args[i];
                // (narrow arguments pushed as whole words: only their low bytes are read)
                if (address == 0x0043D560 || (address == 0x0051DBC0 && i == 0) || (address == 0x0047DC40 && i == 1))
                    raw &= 0xFF; // (bools: SetActive, PlaceTag's sticker, the channel's)
                if (address == 0x0053AEA0 || address == 0x005370A0)
                    raw &= 0xFFFF; // (object ids)
                auto alias = g_aliases.find(raw);
                uint32_t value = alias != g_aliases.end() ? alias->second : Where(r, raw);
                g_trace.push_back(value);
            }
            auto custom = g_custom.find(address);
            if (custom != g_custom.end())
                return custom->second(r);
            auto fixed = g_answers.find(address);
            if (fixed != g_answers.end())
                return fixed->second;
            return Mix(address, g_calls) & 1;
        });
    }
}

struct Callee {
    uint32_t address;
    int pop;
    int thiscall;
    int words;
};

const Callee kCallees[] = {
#include "selftests_skate_module_callees.inc"
};

// The stand-ins: the kit's (memory, structures, scripts, printf) and the generic ones.
void StandInWorld(StandIns& s, const Callee* callees = kCallees, size_t count = sizeof(kCallees) / sizeof(kCallees[0]))
{
    StandInGoalMemory(s);
    StandInPrintf(s);
    StandInGetters(s);
    StandInStructAdds(s);
    StandInContains(s);
    StandInStructChanges(s);
    s.At({0x006234FE}, 0, [](Regs& r) { // sprintf (the game's C runtime): ours, not recorded
        std::vsnprintf(reinterpret_cast<char*>(r.args[0]), 0x100, reinterpret_cast<const char*>(r.args[1]),
                       reinterpret_cast<va_list>(&r.args[2]));
        return 0u;
    });
    StandInApi(s, 0x006450A8, reinterpret_cast<const void*>(&Sleep), 4, [](Regs& r) {
        Record({0x5EE9, r.args[0]});
        return 0u;
    });
    for (size_t k = 0; k < count; ++k) {
        const Callee& c = callees[k];
        void* ours = ReplacementFunction(c.address);
        Generic(s, c.address, ours ? Resolve(ours) : 0u, static_cast<uint16_t>(c.pop), c.thiscall != 0, c.words);
    }
    // (by their code and by the pointers the compiler gives out - incremental linking's jumps)
    auto alias = [](auto function, uint32_t original) {
        g_aliases[AddressOf(function)] = original;
        g_aliases[reinterpret_cast<uint32_t>(function)] = original;
    };
    alias(&Obj::DestroyIfUnlocked, 0x00460AA0);
    alias(&Skate::UpdateSuspendVisibility, 0x00564D30);
    alias(&Skate::LogicTask, 0x00568C90);
}

void SetupWorld()
{
    Setup();
    for (uint32_t address : {0x00701440u, 0x007CE500u, 0x006F0FB8u, 0x007CE4B4u, 0x007CE4BCu, 0x007CE490u, 0x007CE464u,
                             0x006F85A8u, 0x00696D40u, 0x00696D68u, 0x007CE5D8u, 0x006FA7C0u, 0x006FA7C4u, 0x006FA7D4u,
                             0x007CE4A4u, 0x00701378u, 0x007CE480u, 0x007CE484u, 0x007CE47Cu, 0x007CE488u, 0x007D6435u,
                             0x006F3548u})
        SaveGlobal(address, 4);
    SaveGlobal(0x007CC979, 1);
}

uint8_t* Manager(int i) { return g_fakeManagers[i]; }

void ResetWorld()
{
    ResetMemory();
    ResetState();
    g_getAnswers.clear();
    g_typedAnswers.clear();
    g_hasFlag.clear();
    g_hasComponent.clear();
    g_answers.clear();
    g_playerCursors.clear();
    g_calls = 0;
    g_salt = Next();
    GameVariable<bool>(0x007CC979) = true;
    // (every buffer the code writes, cleared: both runs start alike)
    std::memset(g_physicsFlags, 0, sizeof(g_physicsFlags));
    std::memset(g_device, 0, sizeof(g_device));
    std::memset(g_deviceInner, 0, sizeof(g_deviceInner));
    std::memset(g_unknown264, 0, sizeof(g_unknown264));
    std::memset(g_stateHistory, 0, sizeof(g_stateHistory));
    std::memset(g_elements, 0, sizeof(g_elements));
    std::memset(g_conns, 0, sizeof(g_conns));
    std::memset(g_apps, 0, sizeof(g_apps));
    std::memset(g_records, 0, sizeof(g_records));
    // the skate module: the real one's layout, its skater list real
    std::memset(g_skateBuffer, 0, sizeof(g_skateBuffer));
    Skate* skate = TheSkate();
    GameVariable<Skate*>(0x007CE478) = skate;
    Mem::ListNode& head = skate->m_skaters.m_node;
    head.vtable = reinterpret_cast<void**>(0x0064E5D4);
    head.data = reinterpret_cast<void*>(-1);
    head.next = &head;
    head.prev = &head;
    for (int i = 0; i < 26; ++i) {
        std::memset(g_fakeManagers[i], 0, sizeof(g_fakeManagers[i]));
        *reinterpret_cast<void***>(g_fakeManagers[i]) = g_objectVtable;
    }
    skate->m_vtable = reinterpret_cast<void**>(0x0064E674);
    skate->m_career = reinterpret_cast<Obj::CSkaterCareer*>(Manager(24));
    skate->m_railManager = reinterpret_cast<Obj::CRailManager*>(Manager(1));
    skate->m_movies = reinterpret_cast<Obj::CMovieManager*>(Manager(2));
    skate->m_objectMovies = reinterpret_cast<Obj::CMovieManager*>(Manager(3));
    skate->m_gameFlow = reinterpret_cast<Game::CGameFlow*>(Manager(4));
    skate->m_goalManager = reinterpret_cast<Game::CGoalManager*>(Manager(5));
    skate->m_splitscreenPreferences = reinterpret_cast<Prefs::Preferences*>(Manager(6));
    skate->m_competition = reinterpret_cast<Mdl::CCompetition*>(Manager(7));
    skate->m_horse = reinterpret_cast<Mdl::CHorse*>(Manager(8));
    *reinterpret_cast<int*>(Manager(8) + 4) = static_cast<int>(Pick(kSkaters)); // (the horse skater)
    skate->m_checksums = reinterpret_cast<Obj::ChecksumSet*>(Manager(9));
    skate->m_logicTask = reinterpret_cast<Tsk::BaseTask*>(Manager(10));
    skate->m_serverTasks[0] = reinterpret_cast<Tsk::BaseTask*>(Manager(11));
    skate->m_serverTasks[1] = Coin() ? reinterpret_cast<Tsk::BaseTask*>(Manager(12)) : nullptr;
    skate->m_script = Coin() ? reinterpret_cast<Script::CScript*>(Manager(13)) : nullptr;
    skate->m_level = Coin() ? 0xE92ECAFE : 0x1234;
    skate->m_requestedLevel = Pick(3) == 0 ? 0xE92ECAFE : (Coin() ? 0xA368B4F1 : 0x4567);
    skate->m_levelLaunched = Coin();
    skate->m_movieCamera = Coin();
    skate->m_checkControllers = Coin();
    skate->m_unpluggedFrames = static_cast<int>(Pick(4));
    skate->m_levelChanged = Coin();
    skate->m_debugRails = Coin();
    skate->m_gameType = Coin() ? 0x5C8F5D66 : 0x34861A16;
    // the skaters: their vtables record, their nodes at +0x238 put in the list in id order
    for (int i = 0; i < kSkaters; ++i) {
        uint8_t* skater = g_skaters[i];
        std::memset(skater, 0, sizeof(g_skaters[i]));
        *reinterpret_cast<void***>(skater) = g_skaterVtable;
        *reinterpret_cast<uint32_t*>(skater + 0x14) = static_cast<uint32_t>(i);
        skater[0xBC] = static_cast<uint8_t>(i == 0 ? Pick(4) != 0 : Pick(4) == 0);
        *reinterpret_cast<int*>(skater + 0x2BC) = static_cast<int>(Pick(2));
        *reinterpret_cast<uint8_t**>(skater + 0x264) = g_unknown264[i];
        *reinterpret_cast<uint8_t**>(g_unknown264[i] + 0x490) = g_device[i];
        *reinterpret_cast<uint8_t**>(g_device[i] + 0x1C) = g_deviceInner[i];
        *reinterpret_cast<int*>(g_deviceInner[i] + 0xC) = i + 7;
        *reinterpret_cast<uint8_t**>(skater + 0x27C) = g_stateHistory[i];
        skater[0x320] = static_cast<uint8_t>(Pick(4) == 0);
        auto* node = reinterpret_cast<Mem::ListNode*>(skater + 0x238);
        node->vtable = reinterpret_cast<void**>(0x0064E544);
        node->data = skater;
        node->priority = i;
        node->next = node;
        node->prev = node;
        for (int k = 0; k < 8; ++k) {
            std::memset(g_components[i][k], 0, sizeof(g_components[i][k]));
            *reinterpret_cast<void***>(g_components[i][k]) = g_objectVtable;
            *reinterpret_cast<uint8_t**>(g_components[i][k] + 0xC) = skater;
        }
        g_components[i][0][0x13A8] = static_cast<uint8_t>(Coin());
        g_components[i][0][0x13A9] = static_cast<uint8_t>(Coin());
        *reinterpret_cast<uint8_t**>(g_components[i][2] + 0x1528) = g_physicsFlags[i];
        *reinterpret_cast<float*>(g_components[i][6] + 0xA8) = static_cast<float>(Pick(100)) * 0.137f;
        *reinterpret_cast<float*>(g_components[i][6] + 0x50) = static_cast<float>(Pick(100)) * 0.071f;
        *reinterpret_cast<float*>(g_components[i][6] + 0x100) = static_cast<float>(Pick(100)) * 0.03f;
        g_inList[i] = Pick(4) != 0;
        if (g_inList[i]) {
            Mem::ListNode* last = head.prev;
            node->prev = last;
            node->next = &head;
            last->next = node;
            head.prev = node;
        }
    }
    for (int i = 0; i < 6; ++i) {
        std::memset(g_objects[i], 0, sizeof(g_objects[i]));
        *reinterpret_cast<void***>(g_objects[i]) = g_objectVtable;
        g_objectPresent[i] = Coin();
    }
    for (int i = 0; i < 4; ++i) {
        std::memset(g_players[i], 0, sizeof(g_players[i]));
        auto* player = reinterpret_cast<GameNet::PlayerInfo*>(g_players[i]);
        player->m_skater = reinterpret_cast<Obj::CSkater*>(g_skaters[i]);
        player->m_conn = reinterpret_cast<Net::Conn*>(g_conns[i % 3]);
        player->m_team = static_cast<int>(Pick(5)); // (4: past the four teams)
        player->m_eliminatedTime = Coin() ? Pick(150000) : 0;
        std::snprintf(player->m_name, sizeof(player->m_name), "player%d", i);
        g_playerPresent[i] = Pick(4) != 0;
        g_playerLocal[i] = i == 0;
    }
    for (int i = 0; i < 2; ++i) {
        std::memset(g_cameras[i], 0, sizeof(g_cameras[i]));
        g_cameraPresent[i] = Coin();
        g_clientPresent[i] = Coin();
    }
    for (int k = 0; k < 2; ++k) {
        std::memset(g_goals[k], 0, sizeof(g_goals[k]));
        *reinterpret_cast<void***>(g_goals[k]) = g_goalVtables[k];
    }
    // the records' tables
    for (int i = 0; i < 8; ++i)
        std::memset(g_recordTables[i], 0, sizeof(g_recordTables[i]));
    for (int i = 0; i < 7; ++i)
        reinterpret_cast<uint8_t**>(g_records)[i] = g_recordTables[i];
    // the globals
    GameVariable<uint8_t*>(0x007CE500) = Manager(14);
    GameVariable<uint8_t*>(0x006F0FB8) = Manager(15);
    GameVariable<uint8_t*>(0x007CE4B4) = Manager(16);
    GameVariable<uint8_t*>(0x007CE4BC) = Manager(17);
    GameVariable<uint8_t*>(0x007CE490) = Manager(18);
    GameVariable<uint8_t*>(0x007CE464) = Manager(19);
    GameVariable<uint8_t*>(0x006F85A8) = Manager(20);
    GameVariable<uint8_t*>(0x00696D40) = Manager(21);
    GameVariable<uint8_t*>(0x00696D68) = Manager(22);
    GameVariable<uint8_t*>(0x007CE5D8) = Manager(23);
    GameVariable<uint8_t*>(0x006FA7C0) = Manager(0);
    GameVariable<uint8_t*>(0x006FA7C4) = Manager(0) + 0x40;
    GameVariable<uint8_t*>(0x00701440) = Manager(0) + 0x20;
    GameVariable<uint8_t>(0x006FA7D4) = static_cast<uint8_t>(Coin());
    GameVariable<int>(0x007CE4A4) = static_cast<int>(Pick(3) == 0);
    GameVariable<int>(0x00701378) = static_cast<int>(Pick(3));
    GameVariable<void*>(0x007CE480) = Coin() ? Manager(0) + 0x60 : nullptr;
    GameVariable<void*>(0x007CE484) = Coin() ? Manager(0) + 0x70 : nullptr;
    GameVariable<int>(0x007CE47C) = static_cast<int>(Pick(3));
    GameVariable<float>(0x007CE488) = static_cast<float>(Pick(12));
    GameVariable<uint8_t>(0x007D6435) = static_cast<uint8_t>(Coin());
    // the network manager
    g_net[0] = static_cast<uint8_t>(Next() & 1);
    g_net[0x38] = static_cast<uint8_t>(Pick(3));
    *reinterpret_cast<uint8_t**>(g_net + 4) = Coin() ? g_apps[0] : nullptr;
    *reinterpret_cast<uint8_t**>(g_net + 0x6C) = Coin() ? g_objects[5] : nullptr; // (the crown)
    // the game mode
    static const uint32_t kModes[] = {0x6EF8FDA0, 0x5D32129C, 0xBFF33600, 0x3D6D444F, 0x6C5FF266, 0x1C471C60,
                                      0xFC13B531, 0x846504FC, 0xF6198005, 0x10F84276, 0xEC200EAA, 0x12341234};
    g_mode = kModes[Pick(12)];
    g_maxPlayers = static_cast<int>(Pick(5));
    g_answers[0x005532A0] = g_mode;
    g_answers[0x005531D0] = static_cast<uint32_t>(g_maxPlayers);
    g_answers[0x00553190] = 1 + Pick(2); // (the teams: the original divides by it)
    g_answers[0x00553130] = 1 + Pick(4);
    g_answers[0x005535F0] = Pick(3) == 0;
    g_answers[0x00553A60] = Pick(20000);
    g_answers[0x00532C50] = Pick(4);
    g_answers[0x00532FD0] = Pick(5);
    g_answers[0x00539880] = Pick(3);
    g_answers[0x005435F0] = Pick(9000);
    g_answers[0x005436E0] = Pick(9000);
    g_answers[0x00558AB0] = Pick(9);
    g_answers[0x00584130] = Pick(10);
    g_answers[0x005BE970] = 0x1000 + Pick(0x1000);
    g_answers[0x00533080] = 0x2222;
    g_answers[0x005333F0] = Pick(100);
    g_answers[0x0057EFE0] = Coin() ? 0xFFFFFFFF : Pick(5);
    g_answers[0x0044E630] = 0x60 + Pick(4);
    g_answers[0x00561B40] = Pick(50000);
    g_answers[0x00543150] = Pick(500);
    // the restart nodes: by type, or none
    g_custom[0x0046B790] = [](Regs& r) { return Mix(r.args[0], r.args[1]) % 3 ? 0x7E570000u + (r.args[0] & 0xFF) : 0u; };
}

} // namespace

namespace {

alignas(16) uint8_t g_score[0x60];
alignas(16) uint8_t g_profile[0x40];
const char* const kDisplayNames[] = {"Tony", "Bob", "Eric", "Chad"};

// A recording vtable: slot `i` records (the table, the slot, its object and arguments) and answers 0 / 1.
void MakeVtable(StandIns& s, void** table, int which, const uint16_t* pops)
{
    for (int slot = 0; slot < 32; ++slot) {
        uint16_t pop = pops[slot];
        table[slot] = s.Thunk(pop, [which, slot, pop](Regs& r) {
            ++g_calls;
            Record({0xF7000000u | (static_cast<uint32_t>(which) << 8) | static_cast<uint32_t>(slot), Where(r, r.ecx)});
            for (uint32_t i = 0; i < pop / 4u; ++i)
                g_trace.push_back(Where(r, r.args[i]));
            return Mix(0xF7 + which, g_calls) & 1;
        });
    }
}

uint32_t Component(uint32_t object, uint32_t type)
{
    uint32_t i = SkaterIndex(object);
    if (i == 0xFF) {
        if (object == A(g_objects[1])) // (the skater camera)
            return type == 0x27D7CD28 ? A(g_components[0][4]) : 0u;
        return 0u;
    }
    switch (type) {
    case 0x270F56E1: return A(g_components[i][0]); // trick
    case 0xB1D5A3E1: return A(g_components[i][1]); // SkaterEndRun
    case 0x5BD63B29: return A(g_components[i][2]); // SkaterCorePhysics
    case 0x8A897DD2: return A(g_components[i][3]); // shadow
    case 0x27D7CD28: return A(g_components[i][4]); // input
    case 0x404EB936: return (Mix(i, 0x404E) & 3) ? A(g_components[i][5]) : 0u; // vibration
    case 0x589E5FB3: return A(g_components[i][6]); // SkaterBalanceTrick
    case 0xCE0CA665: return (Mix(i, 0xCE0C) & 3) ? A(g_components[i][7]) : 0u; // suspend
    default: return 0u;
    }
}

// The descriptor of a message queued: its fields, and its data (a local's bytes; else where it is).
void RecordDesc(const Regs& r, uint32_t at)
{
    const auto* desc = reinterpret_cast<const Net::MsgDesc*>(at);
    Record({desc->m_id, desc->m_length, static_cast<uint32_t>(desc->m_priority), static_cast<uint32_t>(desc->m_queue),
            desc->m_groupId, Where(r, A(desc->m_data))});
    if (Where(r, A(desc->m_data)) == 0x57AC) {
        const auto* bytes = static_cast<const uint8_t*>(desc->m_data);
        // (the bytes the original leaves as the stack had them: 0x46's padding, 0x80's second word)
        uint32_t length = desc->m_id == 0x46 ? 5u : desc->m_id == 0x80 ? 4u : desc->m_length;
        for (uint32_t i = 0; i < length && i < 0x40; ++i)
            g_trace.push_back(bytes[i]);
    }
}

void StandInAnswers(StandIns& s)
{
    static const uint16_t kSkaterPops[32] = {4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0, 0, 0, 0, 8,
                                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static uint16_t skaterPops[32];
    for (int i = 0; i < 32; ++i)
        skaterPops[i] = kSkaterPops[i];
    skaterPops[0x2C / 4] = 0;
    skaterPops[0x30 / 4] = 0;
    skaterPops[0x34 / 4] = 4;
    skaterPops[0x3C / 4] = 8;
    MakeVtable(s, g_skaterVtable, 0, skaterPops);
    static uint16_t objectPops[32];
    objectPops[0] = 4;
    objectPops[2] = 4;
    MakeVtable(s, g_objectVtable, 1, objectPops);
    static uint16_t goalPops[32];
    MakeVtable(s, g_goalVtables[0], 2, goalPops);
    MakeVtable(s, g_goalVtables[1], 2, goalPops);
    auto self = [](Regs& r) { return r.ecx; };
    for (uint32_t constructor : {0x004738E0u, 0x00552E10u, 0x0055D9C0u, 0x00585270u, 0x0057CC10u, 0x00587120u, 0x0046C470u,
                                 0x005504B0u, 0x005600D0u, 0x0057A4B0u, 0x0044F7E0u})
        g_custom[constructor] = self;
    g_custom[0x0045DB90] = [](Regs& r) { return Component(r.ecx, r.args[0]); };
    g_custom[0x004619F0] = [](Regs& r) {
        uint32_t id = r.args[0];
        int which = id - 0x884EF81Bu < 4 ? 0 : id == 0x967C138C ? 1 : id == 0 ? 2 : id == 1 ? 3 : -1;
        return which >= 0 && g_objectPresent[which] ? A(g_objects[which]) : 0u;
    };
    g_custom[0x0045F2D0] = [](Regs&) { return A(g_components[0][7]); };
    g_custom[0x004A09C0] = [](Regs& r) { return g_cameraPresent[r.args[0] & 1] ? A(g_cameras[r.args[0] & 1]) : 0u; };
    g_custom[0x004A0950] = [](Regs& r) { return g_cameraPresent[(r.args[0] + 1) & 1] ? A(g_cameras[(r.args[0] + 1) & 1]) : 0u; };
    g_custom[0x00485A00] = [](Regs& r) { return r.ecx + 0x40; };
    g_custom[0x004AAE20] = [](Regs& r) {
        uint32_t id = r.args[1];
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        out[0] = (Mix(id, 0xAE20) & 3) ? A(g_elements[id & 3]) : 0u;
        out[1] = 0;
        out[2] = 0;
        return r.args[0];
    };
    g_custom[0x00532EA0] = [](Regs& r) { return g_clientPresent[r.args[0] & 1] ? A(g_apps[r.args[0] & 1]) : 0u; };
    g_custom[0x00458BE0] = [](Regs&) {
        g_connCursor = 0;
        return A(g_conns[0]);
    };
    g_custom[0x004573D0] = [](Regs&) { return ++g_connCursor < 3 ? A(g_conns[g_connCursor]) : 0u; };
    // (each search its own cursor: the searches nest)
    g_custom[0x005372C0] = [](Regs& r) {
        int& cursor = g_playerCursors[r.args[0]];
        for (cursor = 0; cursor < 4; ++cursor)
            if (g_playerPresent[cursor])
                return A(g_players[cursor]);
        return 0u;
    };
    g_custom[0x00537010] = [](Regs& r) {
        int& cursor = g_playerCursors[r.args[0]];
        for (++cursor; cursor < 4; ++cursor)
            if (g_playerPresent[cursor])
                return A(g_players[cursor]);
        return 0u;
    };
    g_custom[0x00537070] = [](Regs& r) { return Mix(0x7070, r.ecx) & 1 ? A(g_players[1]) : 0u; };
    g_custom[0x00537320] = [](Regs&) { return g_playerPresent[0] ? A(g_players[0]) : 0u; };
    g_custom[0x00537480] = [](Regs&) { return g_playerPresent[2] ? A(g_players[2]) : 0u; };
    g_custom[0x0053AEA0] = [](Regs& r) {
        uint32_t id = r.args[0] & 0xFFFF;
        return id < 4 && g_playerPresent[id] ? A(g_players[id]) : 0u;
    };
    g_custom[0x0053A580] = [](Regs&) { return A(g_players[1]); };
    g_custom[0x00543300] = [](Regs& r) {
        for (int i = 0; i < 4; ++i)
            if (r.ecx == A(g_players[i]))
                return g_playerLocal[i] ? 1u : 0u;
        return 0u;
    };
    g_custom[0x005434A0] = [](Regs& r) { return r.ecx & 0xFFF; };
    g_custom[0x00557F90] = [](Regs&) { return A(g_goals[0]); };
    g_custom[0x005591B0] = [](Regs&) { return A(Manager(5)); };
    g_custom[0x0057B640] = [](Regs&) { return A(g_profile); };
    g_custom[0x0057B630] = [](Regs&) { return A(g_profile); };
    g_custom[0x0057A530] = [](Regs& r) { return Mix(0xA530, r.ecx) & 1 ? A(g_objects[4]) : 0u; };
    g_custom[0x0057EEC0] = [](Regs&) { return A(g_records); };
    g_custom[0x00580480] = [](Regs&) { return A(g_score); };
    g_custom[0x00580880] = [](Regs& r) { return Mix(0x0880, r.ecx) & 1 ? A(g_objects[1]) : 0u; };
    g_custom[0x00581580] = [](Regs& r) { return A(kDisplayNames[SkaterIndex(r.ecx) & 3]); };
    g_custom[0x0057FEC0] = [](Regs& r) { return SkaterIndex(r.ecx) & 3; };
    g_custom[0x00582D90] = [](Regs& r) { // CSkater::Construct: a fake skater (its vtable, id, local, viewport, node)
        auto* skater = reinterpret_cast<uint8_t*>(r.ecx);
        *reinterpret_cast<void***>(skater) = g_skaterVtable;
        *reinterpret_cast<uint32_t*>(skater + 0x14) = r.args[3];
        skater[0xBC] = static_cast<uint8_t>(r.args[2]);
        *reinterpret_cast<int*>(skater + 0x2BC) = static_cast<int>(r.args[0]);
        auto* node = reinterpret_cast<Mem::ListNode*>(skater + 0x238);
        node->vtable = reinterpret_cast<void**>(0x0064E544);
        node->data = skater;
        node->priority = 0;
        node->next = node;
        node->prev = node;
        return r.ecx;
    };
    g_custom[0x0058D690] = [](Regs&) { return A(Manager(0) + 0x50); };
    g_custom[0x004F4970] = [](Regs&) { return A(Manager(0) + 0x58); };
    g_custom[0x004F45B0] = [](Regs&) { return A(Manager(0) + 0x5C); };
    g_custom[0x00478FF0] = [](Regs& r) { return A(reinterpret_cast<const char*>(r.args[0])); };
    g_custom[0x00576EA0] = [](Regs&) { return g_objectPresent[5] ? A(g_objects[5]) : 0u; };
    g_custom[0x00532F70] = [](Regs&) { return A(Manager(0) + 0x34); };
    g_custom[0x00532F80] = [](Regs&) { return A(Manager(0) + 0x38); };
    g_custom[0x005333E0] = [](Regs&) { return A(Manager(0) + 0x3C); };
    g_custom[0x00584110] = [](Regs&) { return A(Manager(0) + 0x44); };
    g_custom[0x00478F60] = [](Regs& r) { return Mix(r.args[0], 0x8F60) % 4; }; // the script integers
    // (the messages queued: their descriptors' fields and local data)
    g_custom[0x00457EB0] = [](Regs& r) {
        RecordDesc(r, r.args[1]);
        return 0u;
    };
    g_custom[0x00459E50] = [](Regs& r) {
        RecordDesc(r, r.args[0]);
        return 0u;
    };
}

// Each of the module's skaters, the list and the module's fields as words.
void RecordSkate()
{
    auto* words = reinterpret_cast<uint32_t*>(g_skateBuffer);
    for (uint32_t i = 1; i < sizeof(Skate) / 4; ++i) // (the vtable aside)
        g_trace.push_back(words[i]);
    Mem::ListNode* head = &TheSkate()->m_skaters.m_node;
    int n = 0;
    for (Mem::ListNode* node = head->next; node != head && n < 16; node = node->next, ++n)
        Record({A(node->data), static_cast<uint32_t>(node->priority)});
    for (int i = 0; i < kSkaters; ++i)
        Record({*reinterpret_cast<uint32_t*>(g_skaters[i] + 0x08), *reinterpret_cast<uint32_t*>(g_skaters[i] + 0x14),
                g_components[i][2][0x2D9], g_components[i][2][0x2DA]});
    for (int i = 0; i < 4; ++i)
        Record({*reinterpret_cast<uint32_t*>(g_players[i] + 0x1D0)});
    for (int k = 1; k < 8; ++k) // (the logic task: its mask)
        g_trace.push_back(reinterpret_cast<uint32_t*>(Manager(10))[k]);
    Record({GameVariable<uint32_t>(0x007CE480), GameVariable<uint32_t>(0x007CE484), GameVariable<uint32_t>(0x007CE47C),
            GameVariable<uint32_t>(0x007CE488), GameVariable<uint32_t>(0x007CE478)});
}

template <typename R, typename... Args>
R Call(bool original, uint32_t address, R (Skate::*ours)(Args...), Args... args)
{
    if (original)
        return Original<R(__thiscall*)(Skate*, Args...)>(address)(TheSkate(), args...);
    return (TheSkate()->*ours)(args...);
}
template <typename R, typename... Args>
R CallStatic(bool original, uint32_t address, R(__cdecl* ours)(Args...), Args... args)
{
    if (original)
        return Original<R(__cdecl*)(Args...)>(address)(args...);
    return ours(args...);
}

} // namespace

namespace {

alignas(16) uint8_t g_params[4][0x10];
CStruct* ParamsAt(int i) { return reinterpret_cast<CStruct*>(g_params[i]); }
alignas(16) uint8_t g_task[0x40];
void* g_taskCode;
alignas(16) uint8_t g_script[0x100];

struct Fixture {
    StandIns* standIns;
    Fixture()
    {
        SetupWorld();
        standIns = new StandIns;
        StandInWorld(*standIns);
        StandInAnswers(*standIns);
        g_taskCode = standIns->Thunk(0, [](Regs& r) {
            Record({0x7A5C, Where(r, r.args[0])});
            return 0u;
        });
    }
};

void SeedParams()
{
    std::memset(g_score, 0, sizeof(g_score));
    std::memset(g_profile, 0, sizeof(g_profile));
    for (int i = 0; i < 4; ++i) {
        uint32_t p = A(g_params[i]);
        static const uint32_t kLevels[] = {0x547CB588, 0xB664035D, 0x1111, 0xE92ECAFE};
        g_getAnswers[{p, 0x651533EC}] = kLevels[Pick(4)];           // level
        g_typedAnswers[{p, 0}][0x6950] = Coin() ? 0x2345u : 0x547CB588u; // the unnamed checksum
        if (Coin())
            g_typedAnswers[{p, 0}][0x6590] = A("park");              // the unnamed string
        if (Coin())
            g_getAnswers[{p, 0x5B8AB877}] = Pick(3);                 // skater
        static const uint32_t kPrefs[] = {0x9F7B7843, 0x4839873C, 0x06AB02F2, 0x99};
        g_getAnswers[{p, 0xB58E6556}] = kPrefs[Pick(4)];             // prefs
        if (Coin())
            g_hasFlag.insert({p, 0x68910AC6});                       // AbsentInNetGames
        if (Coin())
            g_hasComponent.insert({p, 0xD8E34EDA});                  // show_warning
    }
}

// --- the module's state and commands --------------------------------------------------------------------------------

void TestSkateModuleState()
{
    Fixture fixture;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        for (uint32_t seed = 0; seed < 250; ++seed) {
            g_seed = seed * 2654435761u + 3;
            ResetWorld();
            SeedParams();
            Skate* skate = TheSkate();
            Record({0xEEEE0000 | seed});
            // the singleton
            if (Coin()) {
                skate->m_vtable = g_objectVtable;
                if (o)
                    Original<void(__cdecl*)()>(0x00564C10)();
                else
                    Skate::DeleteInstance();
                RecordSkate();
                skate->m_vtable = reinterpret_cast<void**>(0x0064E674);
                GameVariable<Skate*>(0x007CE478) = skate;
            }
            if (Coin()) {
                if (Pick(4) == 0)
                    GameVariable<Skate*>(0x007CE478) = nullptr;
                bool create = Coin();
                Skate* made = o ? Original<Skate*(__cdecl*)(bool)>(0x005691A0)(create) : Skate::CreateInstance(create);
                Record({made == skate ? 2u : made ? 1u : 0u, GameVariable<uint32_t>(0x007CE478) ? 1u : 0u,
                        GameVariable<uint32_t>(0x007CE47C)});
                GameVariable<Skate*>(0x007CE478) = skate;
            }
            Call<void>(o, 0x00564C50, &Skate::StopCallback);
            Call<void, Obj::CSkater*, bool>(o, 0x00564C60, &Skate::HideSkater,
                                            reinterpret_cast<Obj::CSkater*>(g_skaters[Pick(kSkaters)]), Coin());
            static const float kTimes[] = {0.0f, 0.01f, 3.0f, 9.5f, 10.0f, 12.0f, 0.5f};
            for (int k = 0; k < 3; ++k)
                Call<void, float, float, float, const char*>(o, 0x00564CA0, &Skate::TimerBeep, kTimes[Pick(7)],
                                                             10.0f, Coin() ? 10.0f : 400.0f, "beep");
            Call<void, int>(o, 0x00564D50, &Skate::SetUnknown370, static_cast<int>(Pick(9)));
            Call<void>(o, 0x00564D60, &Skate::UpdateGameFlow);
            Record({A(Call<Game::CGoalManager*>(o, 0x00564D90, &Skate::GetGoalManager))});
            Record({A(skate->GetSplitScreenPreferences())});
            if (o)
                Original<void(__thiscall*)(Skate*)>(0x00564DA0)(skate);
            else
                skate->CheckControllers();
            *reinterpret_cast<void**>(g_task + 0x18) = g_taskCode;
            if (o)
                Original<void(__thiscall*)(void*)>(0x00564DB0)(g_task);
            else
                Skate::RunTask(reinterpret_cast<Tsk::BaseTask*>(g_task));
            if (Pick(4) == 0)
                Call<void>(o, 0x00564DC0, &Skate::StartCallback);
            Call<void>(o, 0x00564F20, &Skate::CleanupForParkEditor);
            Call<void, bool>(o, 0x00564F60, &Skate::Pause, Coin());
            using ByParams = Obj::CSkaterProfile* (Skate::*)(Script::CStruct*);
            using ByIndex = Obj::CSkaterProfile* (Skate::*)(int);
            Record({A(Call<Obj::CSkaterProfile*, Script::CStruct*>(o, 0x00564F80, static_cast<ByParams>(&Skate::GetProfile),
                                                                   Coin() ? ParamsAt(0) : nullptr))});
            Record({A(Call<Obj::CSkaterProfile*, int>(o, 0x00564FC0, static_cast<ByIndex>(&Skate::GetProfile),
                                                      static_cast<int>(Pick(3))))});
            Call<void>(o, 0x00564FD0, &Skate::SetCurrentGameType);
            Call<void, uint32_t>(o, 0x00565000, &Skate::OpenLevel, Coin() ? 0x3A8E4A31u : 0x4321u);
            RecordSkate();
            CallStatic<void>(o, 0x005651D0, &Skate::ObserveNextSkater);
            Record({static_cast<uint32_t>(CallStatic<int>(o, 0x005651F0, &Skate::IsMultiplayer))});
            // the commands
            using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
            struct Command {
                uint32_t address;
                CFunc ours;
            };
            const Command kCommands[] = {{0x00565220, &Skate::CFunc_SetGameType},
                                         {0x00565260, &Skate::CFunc_ChangeLevel},
                                         {0x005654B0, &Skate::CFunc_LaunchLevel},
                                         {0x005654D0, &Skate::CFunc_RequestLevel},
                                         {0x005655B0, &Skate::CFunc_InitSkaterHeaps}};
            for (const Command& c : kCommands) {
                CStruct* params = ParamsAt(static_cast<int>(Pick(4)));
                if (c.address == 0x005655B0 && Coin())
                    skate->m_gameType = 0x5C8F5D66; // (freeskate2p)
                Record({o ? Original<CFunc>(c.address)(params, nullptr) : c.ours(params, nullptr)});
                RecordSkate();
            }
            Record({static_cast<uint32_t>(o ? Original<int(__cdecl*)(CStruct*, Script::CScript*)>(0x00565250)(nullptr, nullptr)
                                            : Skate::CFunc_InTeamGame(nullptr, nullptr))});
            Call<void>(o, 0x00565620, &Skate::PreCalculateObjectUpdateInfo);
            static const uint32_t kNames[] = {0x9F7B7843, 0x4839873C, 0x06AB02F2, 0x99};
            Record({A(CallStatic<Prefs::Preferences*, uint32_t>(o, 0x00565680, &Skate::GetPreferences, kNames[Pick(4)]))});
            Record({A(CallStatic<Prefs::Preferences*, CStruct*>(o, 0x00565D40, &Skate::GetPreferencesFromParams,
                                                                ParamsAt(static_cast<int>(Pick(4)))))});
            Record({A(Call<void*>(o, 0x005656C0, &Skate::GetGapChecklist))});
            Record({static_cast<uint32_t>(Call<int>(o, 0x005656D0, &Skate::InNetGameLevel))});
            if (Coin())
                skate->m_requestedLevel = Coin() ? 0xA368B4F1 : 0xE92ECAFE; // (load_cas / the main menu)
            Record({Call<bool>(o, 0x00565700, &Skate::InFrontEndOrNetMode)});
            CStruct* absent = ParamsAt(static_cast<int>(Pick(4)));
            Record({static_cast<uint32_t>(o ? Original<int(__stdcall*)(CStruct*)>(0x00565BB0)(absent)
                                            : Skate::ShouldBeAbsent(absent))});
            Record({Call<uint32_t>(o, 0x005695A0, &Skate::GetCheats)});
            Call<void>(o, 0x00569580, &Skate::BeginQueuedScripts);
            RecordSkate();
            Call<void>(o, 0x00569590, &Skate::EndQueuedScripts);
            RecordSkate();
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "skate module state: the original ran");
    CompareTraces(traces[0], traces[1], "skate module state (0x00564C10-0x00565D9F)");
}

void RestoreSkate() { Restore(); }

} // namespace

SELFTEST(Early, TestSkateModuleState, RestoreSkate);

namespace {

// --- the skaters and the level --------------------------------------------------------------------------------------

alignas(16) uint8_t g_headBuffer[0x20];

void TestSkateModuleSkaters()
{
    Fixture fixture;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        for (uint32_t seed = 0; seed < 250; ++seed) {
            g_seed = seed * 2654435761u + 7;
            ResetWorld();
            SeedParams();
            Skate* skate = TheSkate();
            Record({0xEEEE0000 | seed});
            int count = skate->GetNumSkaters();
            // the lookups
            for (int k = 0; k < 3; ++k) {
                int index = static_cast<int>(Pick(6));
                Record({A(Call<Obj::CCompositeObject*, int>(o, 0x005665C0, &Skate::GetSkater, index))});
                uint32_t id = Pick(6);
                Record({A(Call<Obj::CSkater*, uint32_t>(o, 0x00566600, &Skate::GetSkaterById, id))});
            }
            if (count) // (the original walks the list's nodes: no skater in it would read the head as one)
                Record({A(Call<Obj::CSkater*>(o, 0x00566520, &Skate::GetLocalSkater))});
            Record({static_cast<uint32_t>(Call<int, bool>(o, 0x00565FF0, &Skate::GetNextUnusedSkaterHeapIndex, Coin()))});
            if (count) { // (the original divides by the skaters' count)
                uint32_t teams = g_answers[0x005535F0];
                if (Coin()) { // (capture the flag's restarts: by the local player's team)
                    g_answers[0x005532A0] = 0x6C5FF266;
                    g_answers[0x005535F0] = 1;
                }
                Record({Call<uint32_t, uint32_t>(o, 0x00566100, &Skate::SkipToRestartPoint, Pick(5))});
                g_answers[0x005532A0] = g_mode;
                g_answers[0x005535F0] = teams;
            }
            if (count)
                Call<void, Obj::CSkater*, uint32_t, uint32_t>(o, 0x00566370, &Skate::MoveToRestart,
                                                          reinterpret_cast<Obj::CSkater*>(g_skaters[Pick(kSkaters)]),
                                                          Coin() ? 0u : 0x5050u, Pick(2));
            if (count)
                Call<void, uint32_t, uint32_t>(o, 0x00566980, &Skate::ResetSkaters, Coin() ? 0u : 0x5051u, Pick(2));
            Call<void>(o, 0x00567090, &Skate::BindControllers);
            Call<void, Obj::CSkater*>(o, 0x00565BF0, &Skate::SetSkaterShadow,
                                      reinterpret_cast<Obj::CSkater*>(g_skaters[Pick(kSkaters)]));
            for (int k = 0; k < 2; ++k) {
                int player = static_cast<int>(Pick(8));
                Call<void, int, bool>(o, 0x00567BD0, &Skate::SetSpinTaps, player, Coin());
                Call<void, int, bool>(o, 0x00567C00, &Skate::SetAutoKick, player, Coin());
                Call<void, int, int>(o, 0x00567C30, &Skate::SetVibration, player, static_cast<int>(Pick(3) * 0x100 | Pick(2)));
            }
            RecordSkate();
            // the runs and records
            if (count) {
                Record({Call<bool>(o, 0x00567580, &Skate::AllSkatersIdle)});
                if (skate->GetSkaterById(static_cast<uint32_t>(*reinterpret_cast<int*>(Manager(8) + 4)))) {
                    Record({Call<bool>(o, 0x00567690, &Skate::FirstTrickStarted)});
                    Record({Call<bool>(o, 0x005676E0, &Skate::FirstTrickCompleted)});
                }
            }
            Call<void>(o, 0x00566F40, &Skate::UpdateRecords);
            Call<void>(o, 0x00566BE0, &Skate::CheckSkaterCollisions);
            RecordSkate();
            // the script
            Call<void, uint32_t, Script::CStruct*>(o, 0x00565ED0, &Skate::RunScript, 0x5C5C, ParamsAt(0));
            Record({A(skate->m_script) ? 1u : 0u});
            Record({Call<bool>(o, 0x00565F80, &Skate::KillScript)});
            CallStatic<void>(o, 0x00565DA0, &Skate::DebugRails);
            CallStatic<void, Obj::CObject*, void*>(o, 0x00564D30, &Skate::UpdateSuspendVisibility,
                                                   reinterpret_cast<Obj::CObject*>(g_skaters[Pick(kSkaters)]), nullptr);
            // the session
            switch (Pick(6)) {
            case 0: Call<void, uint32_t>(o, 0x00565A00, &Skate::ChangeLevel, Coin() ? 0xE92ECAFEu : 0x777u); break;
            case 1: Call<void>(o, 0x005666A0, &Skate::Cleanup); break;
            case 2: Call<void>(o, 0x005668A0, &Skate::RestartLevel); break;
            case 3: Call<void>(o, 0x00566900, &Skate::ResetLevel); break;
            case 4: Call<void>(o, 0x00566A60, &Skate::LaunchGame); break;
            default: {
                using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
                std::memset(g_script, 0, sizeof(g_script));
                g_script[0xCC] = static_cast<uint8_t>(Next());
                auto* script = Coin() ? reinterpret_cast<Script::CScript*>(g_script) : nullptr;
                uint32_t which = Pick(3);
                static const uint32_t kAddresses[] = {0x00566E70, 0x00566EA0, 0x00566ED0};
                const CFunc kOurs[] = {&Skate::CFunc_Retry, &Skate::CFunc_LaunchGame, &Skate::CFunc_ResetLevel};
                Record({o ? Original<CFunc>(kAddresses[which])(nullptr, script) : kOurs[which](nullptr, script)});
                Record({g_script[0xCC]});
                break;
            }
            }
            RecordSkate();
            // a skater removed / added
            if (count && Coin()) {
                uint8_t* skater = static_cast<uint8_t*>(skate->m_skaters.m_node.next->data);
                Call<void, Obj::CSkater*>(o, 0x00565790, &Skate::RemoveSkater, reinterpret_cast<Obj::CSkater*>(skater));
                RecordSkate();
            }
            {
                bool local = Coin();
                uint32_t id = Pick(6);
                Record({A(Call<Obj::CSkater*, Obj::CSkaterProfile*, bool, uint32_t, int>(
                           o, 0x00567180, &Skate::AddSkater, reinterpret_cast<Obj::CSkaterProfile*>(g_profile), local, id,
                           static_cast<int>(Pick(2))))});
                RecordSkate();
            }
            // the list's node and head destructors, the head's body
            {
                auto* node = reinterpret_cast<Mem::ListNode*>(g_headBuffer);
                std::memset(g_headBuffer, 0, sizeof(g_headBuffer));
                node->next = node;
                node->prev = node;
                uint8_t flags = static_cast<uint8_t>(Pick(2));
                uint32_t which = Pick(3);
                if (which == 0)
                    Record({A(o ? Original<Mem::ListNode*(__thiscall*)(void*, uint8_t)>(0x00565750)(node, flags)
                                : Skate::DestroySkaterNode(node, 0, flags))});
                else if (which == 1)
                    Record({A(o ? Original<Mem::ListNode*(__thiscall*)(void*, uint8_t)>(0x00565FC0)(node, flags)
                                : Skate::DestroySkaterHead(node, 0, flags))});
                else if (o)
                    Original<void(__thiscall*)(void*)>(0x00565730)(node);
                else
                    Mdl::SkaterHeadDestroyBody(node);
                Record({*reinterpret_cast<uint32_t*>(g_headBuffer), *reinterpret_cast<uint32_t*>(g_headBuffer + 0xC)});
            }
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "skate module skaters: the original ran");
    CompareTraces(traces[0], traces[1], "skate module skaters (0x00565790-0x00567C6F)");
}

// --- each frame, made and destroyed ---------------------------------------------------------------------------------

void TestSkateModuleFrame()
{
    Fixture fixture;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        for (uint32_t seed = 0; seed < 250; ++seed) {
            g_seed = seed * 2654435761u + 11;
            ResetWorld();
            SeedParams();
            if (Pick(4) == 0) { // (no skaters)
                Mem::ListNode& head = TheSkate()->m_skaters.m_node;
                head.next = &head;
                head.prev = &head;
            }
            Skate* skate = TheSkate();
            Record({0xEEEE0000 | seed});
            if (skate->GetNumSkaters()) {
                Call<void>(o, 0x00567710, &Skate::DoUpdate);
                RecordSkate();
            }
            {
                *reinterpret_cast<Skate**>(g_task + 0x1C) = skate;
                if (o)
                    Original<void(__cdecl*)(void*)>(0x00568C90)(g_task);
                else
                    Skate::LogicTask(reinterpret_cast<Tsk::BaseTask*>(g_task));
                RecordSkate();
            }
            switch (Pick(3)) {
            case 0: {
                // made in fresh memory (its managers stood in for)
                std::memset(g_skateBuffer, 0xCD, sizeof(g_skateBuffer));
                Record({A(Call<Skate*>(o, 0x00568DE0, &Skate::Construct))});
                RecordSkate();
                break;
            }
            case 1:
                Call<void>(o, 0x005663A0, &Skate::DestroyBody);
                RecordSkate();
                break;
            default:
                Record({A(Call<Skate*, uint8_t>(o, 0x00567BA0, &Skate::Destroy, static_cast<uint8_t>(Pick(2))))});
                RecordSkate();
                break;
            }
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 20000, "skate module frame: the original ran");
    CompareTraces(traces[0], traces[1], "skate module frame / construction (0x00567710, 0x00568C90-0x00568FFF)");
}

// --- the rankings ---------------------------------------------------------------------------------------------------

void TestSkateRankings()
{
    Fixture fixture;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        for (uint32_t seed = 0; seed < 300; ++seed) {
            g_seed = seed * 2654435761u + 13;
            ResetWorld();
            SeedParams();
            Record({0xEEEE0000 | seed});
            using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
            if (Coin())
                Record({o ? Original<CFunc>(0x00568120)(nullptr, nullptr) : Skate::CFunc_FillRankingScreen(nullptr, nullptr)});
            else if (o)
                Original<void(__cdecl*)()>(0x00567C70)();
            else
                Mdl::FillEliminationRanking();
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 5000, "skate rankings: the original ran");
    CompareTraces(traces[0], traces[1], "skate rankings (0x00567C70, 0x00568120)");
}

} // namespace

SELFTEST(Early, TestSkateModuleSkaters, RestoreSkate);
SELFTEST(Early, TestSkateModuleFrame, RestoreSkate);
SELFTEST(Early, TestSkateRankings, RestoreSkate);

namespace {

// --- the skate-the-line goal, the state history's newest entries ----------------------------------------------------

alignas(16) uint8_t g_lineGoal[0x80];
alignas(16) uint8_t g_clusters[3][0x10];
uint32_t g_clusterElements[3];
CArray g_clusterArray;

void TestSkateLineGoal()
{
    Fixture fixture;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        for (uint32_t seed = 0; seed < 300; ++seed) {
            g_seed = seed * 2654435761u + 17;
            ResetWorld();
            SeedParams();
            Record({0xEEEE0000 | seed});
            std::memset(g_lineGoal, 0, sizeof(g_lineGoal));
            auto* goal = reinterpret_cast<Game::CSkateLineGoal*>(g_lineGoal);
            goal->m_node.vtable = reinterpret_cast<void**>(g_goalVtables[0]);
            goal->m_params = ParamsAt(1);
            goal->m_numFlagsSet = static_cast<int>(Pick(3));
            std::memset(&g_clusterArray, 0, sizeof(g_clusterArray));
            g_clusterArray.m_size = 3;
            for (int k = 0; k < 3; ++k) {
                g_clusterElements[k] = A(g_clusters[k]);
                g_getAnswers[{A(g_clusters[k]), 0x2E0B1465}] = 0xF1A60000u + static_cast<uint32_t>(k); // flag
                if (Coin())
                    g_getAnswers[{A(g_clusters[k]), 0x906B67BA}] = Pick(30); // time
            }
            g_clusterArray.m_elements = g_clusterElements;
            g_getAnswers[{A(ParamsAt(1)), 0x3CCE31F0}] = A(&g_clusterArray); // goal_objects
            if (Coin())
                g_hasFlag.insert({A(ParamsAt(1)), 0x7100155D});
            Record({o ? Original<bool(__thiscall*)(void*)>(0x00569220)(goal) : goal->Land()});
            if (o)
                Original<void(__thiscall*)(void*)>(0x005692C0)(goal);
            else
                goal->NextCluster();
            uint32_t flag = 0xF1A60000u + Pick(3);
            int value = static_cast<int>(Pick(2));
            uint32_t sub = Pick(2);
            Record({o ? Original<bool(__thiscall*)(void*, uint32_t, int, uint32_t)>(0x00569410)(goal, flag, value, sub)
                      : goal->SetFlag(flag, value, sub)});
            switch (Pick(3)) {
            case 0:
                Record({A(o ? Original<Game::CSkateLineGoal*(__thiscall*)(void*, CStruct*)>(0x005691F0)(goal, ParamsAt(2))
                            : goal->Construct(ParamsAt(2)))});
                break;
            case 1:
                if (o)
                    Original<void(__thiscall*)(void*)>(0x00569210)(goal);
                else
                    goal->DestroyBody();
                break;
            default: {
                uint8_t flags = static_cast<uint8_t>(Pick(2));
                Record({A(o ? Original<Game::CSkateLineGoal*(__thiscall*)(void*, uint8_t)>(0x00569290)(goal, flags)
                            : goal->Destroy(flags))});
                break;
            }
            }
            Record({*reinterpret_cast<uint32_t*>(g_lineGoal)});
            // the state history
            auto* history = reinterpret_cast<Obj::CSkaterStateHistoryComponent*>(g_stateHistory[0]);
            *reinterpret_cast<int*>(g_stateHistory[0] + 0x18) = static_cast<int>(Pick(60));
            *reinterpret_cast<int*>(g_stateHistory[0] + 0xA6C) = static_cast<int>(Pick(60));
            Record({A(o ? Original<uint8_t*(__thiscall*)(void*)>(0x00569680)(history) : history->GetLatestState()),
                    A(o ? Original<void*(__thiscall*)(void*)>(0x005696A0)(history) : history->GetLatestMessage())});
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 3000, "skate line goal: the original ran");
    CompareTraces(traces[0], traces[1], "skate line goal / state history (0x005691F0-0x005696BF)");
}

} // namespace

SELFTEST(Early, TestSkateLineGoal, RestoreSkate);

// --- the network side: the server's tasks and relays, the client's handlers (0x00569560-0x0056C91F) -----------------

namespace {

const Callee kRelayCallees[] = {
#include "selftests_skate_net_relay_callees.inc"
};

alignas(16) uint8_t g_msg[0x200];
alignas(16) uint8_t g_extra[6][0x220]; // the score's holder, tagging, model (+0x208 its model), the model, the blender
alignas(16) uint8_t g_texture[0x40];
void* g_textureVtable[32];
alignas(16) uint8_t g_spawned[0x100];  // the script SpawnScript makes
alignas(16) uint8_t g_relayTask[0x40];

using MsgCode = int(__cdecl*)(Net::MsgHandlerContext*);
using SkateNet = Mdl::SkateNet;

void RelayAnswers(StandIns& s)
{
    static uint16_t texturePops[32];
    texturePops[5] = 16;
    MakeVtable(s, g_textureVtable, 3, texturePops);
    g_custom[0x0053AE10] = [](Regs& r) { // PlayerByConnection
        for (int i = 0; i < 4; ++i)
            if (g_playerPresent[i] && reinterpret_cast<GameNet::PlayerInfo*>(g_players[i])->m_conn ==
                                          reinterpret_cast<Net::Conn*>(r.args[0]))
                return A(g_players[i]);
        return 0u;
    };
    g_custom[0x004619F0] = [](Regs& r) { // GetObjectByID: the skaters by id (some missing)
        uint32_t id = r.args[0];
        return id < kSkaters && (Mix(id, 0x19F0) & 3) ? A(g_skaters[id]) : 0u;
    };
    g_custom[0x0045DB90] = [](Regs& r) {
        uint32_t i = SkaterIndex(r.ecx);
        switch (r.args[0]) {
        case 0xD262802F: return A(g_extra[0]);                                     // SkaterScore
        case 0x5B512EDC: return (Mix(r.ecx, 0xEDC) & 3) ? A(g_extra[1]) : 0u;      // Tagging
        case 0x286A8D26: return A(g_extra[2]);                                     // model
        case 0x7149B753: return (Mix(r.ecx, 0xB753) & 3) ? A(g_extra[4]) : 0u;     // animblender
        case 0x18223FD6: return i < kSkaters ? A(g_stateHistory[i]) : 0u;          // SkaterStateHistory
        default: return Component(r.ecx, r.args[0]);
        }
    };
    g_custom[0x005430B0] = [](Regs&) { return A(g_texture); };
    g_custom[0x00475420] = [](Regs&) { return A(g_spawned); };
    g_custom[0x004875C0] = [](Regs& r) { return r.ecx; };
    g_custom[0x00532EC0] = [](Regs& r) {
        uint32_t k = Mix(r.esp & 0, 0xEC0) % 3; // (the seed's: not where it's called from)
        return k ? A(g_players[k]) : 0u;
    };
    g_custom[0x00444BB0] = [](Regs& r) {
        const auto* v = reinterpret_cast<const float*>(r.args[2]);
        uint32_t bits[3];
        std::memcpy(bits, v, sizeof(bits));
        Record({bits[0], bits[1], bits[2]});
        return 0u;
    };
}

struct RelayFixture {
    StandIns* standIns;
    RelayFixture()
    {
        SetupWorld();
        SaveGlobal(0x007CE48C, 1);
        standIns = new StandIns;
        StandInWorld(*standIns, kRelayCallees, sizeof(kRelayCallees) / sizeof(kRelayCallees[0]));
        StandInAnswers(*standIns);
        RelayAnswers(*standIns);
    }
};

struct Handler {
    uint32_t address;
    MsgCode ours;
    std::vector<uint8_t> ids;
};

void SetUpRelay()
{
    Skate* skate = TheSkate();
    skate->m_serverTasks[1] = reinterpret_cast<Tsk::BaseTask*>(Manager(12)); // (LeaveServer removes both)
    for (uint32_t i = 0; i < sizeof(g_msg); ++i)
        g_msg[i] = static_cast<uint8_t>(Next());
    for (uint32_t at : {0u, 1u, 4u, 8u, 0xCu, 0x34u})
        g_msg[at] = static_cast<uint8_t>(Pick(5)); // (the object ids)
    std::memset(g_extra, 0, sizeof(g_extra));
    *reinterpret_cast<uint8_t**>(g_extra[0] + 0x18) = Pick(4) ? g_score : nullptr;
    *reinterpret_cast<uint8_t**>(g_extra[2] + 0x208) = g_extra[3];
    std::memset(g_texture, 0, sizeof(g_texture));
    *reinterpret_cast<void***>(g_texture) = g_textureVtable;
    std::memset(g_spawned, 0, sizeof(g_spawned));
    for (int i = 0; i < 0x60; ++i)
        g_score[i] = static_cast<uint8_t>(Next());
    GameVariable<uint8_t>(0x007CE48C) = static_cast<uint8_t>(Pick(5));
    g_net[0x18C] = static_cast<uint8_t>(Coin());
    *reinterpret_cast<uint8_t**>(g_net + 4) = g_apps[0];
    *reinterpret_cast<uint32_t*>(g_apps[0] + 8) = Pick(8000);
    for (int i = 0; i < 3; ++i)
        *reinterpret_cast<int*>(g_conns[i] + 0x1CA4) = static_cast<int>(Pick(300));
    // the histories: their counts and newest times; what each player was sent
    for (int i = 0; i < kSkaters; ++i) {
        uint8_t* history = g_stateHistory[i];
        *reinterpret_cast<int*>(history + 0x18) = static_cast<int>(Pick(30));
        *reinterpret_cast<int*>(history + 0xA6C) = static_cast<int>(Pick(30));
        for (int e = 0; e < 20; ++e)
            *reinterpret_cast<uint16_t*>(history + 0x1C + e * 0x84 + 0x78) = static_cast<uint16_t>(Pick(4));
    }
    for (int p = 0; p < 4; ++p) {
        for (int i = 0; i < 8; ++i)
            *reinterpret_cast<uint32_t*>(g_players[p] + 0x70 + i * 4) = Pick(4);
        reinterpret_cast<GameNet::PlayerInfo*>(g_players[p])->m_unknown34 = static_cast<int>(Pick(7));
    }
    for (int i = 0; i < kSkaters; ++i)
        for (int k = 0; k < 3; ++k)
            *reinterpret_cast<float*>(g_skaters[i] + 0x4C + k * 4) = static_cast<float>(static_cast<int>(Pick(2400)) - 1200);
    g_answers[0x00543140] = 1 + Pick(3);      // GetSkatersPerUpdate
    g_answers[0x00543120] = Pick(8);          // GetLastSentSkater
    g_answers[0x00533070] = Pick(2);          // GetGameId
    g_answers[0x005533B0] = Pick(100000);     // GetGoalTimeLeft
    {
        uint32_t now = *reinterpret_cast<uint32_t*>(g_apps[0] + 8);
        g_answers[0x005331C0] = Coin() ? now - 1999 - Pick(3) : Pick(8000); // GetLastScoreUpdate (2000 ms ago: the edge)
    }
    g_answers[0x00532AD0] = Pick(3);          // GetHostMode
    g_answers[0x00455B40] = Pick(500);        // GetAveLatency
    g_answers[0x004559A0] = Pick(3);          // GetBandwidthType
}

// The message for one handler: its counts small, its floats in range.
void ShapeMessage(uint32_t address, uint8_t id)
{
    auto word = [](uint32_t at) -> uint32_t& { return *reinterpret_cast<uint32_t*>(g_msg + at); };
    auto real = [](uint32_t at) -> float& { return *reinterpret_cast<float*>(g_msg + at); };
    switch (address) {
    case 0x0056AD10:
    case 0x0056B0B0: {
        // the hunt's counts near what the players dropped: all, five or one left; a dropper at five
        int total = 0, players = 0;
        for (int i = 0; i < 4; ++i)
            if (g_playerPresent[i]) {
                total += reinterpret_cast<GameNet::PlayerInfo*>(g_players[i])->m_unknown34;
                ++players;
            }
        static const int kOffsets[] = {0, -5, -1, 3};
        int collected = total + kOffsets[Pick(4)];
        g_answers[0x00561B40] = static_cast<uint32_t>(Coin() ? collected : total - 1);
        g_answers[0x005435F0] = static_cast<uint32_t>(collected);
        if (address == 0x0056B0B0 && Coin()) // (the drop that makes it five each, or more)
            for (int i = 0; i < 4; ++i)
                reinterpret_cast<GameNet::PlayerInfo*>(g_players[i])->m_unknown34 = 5 - static_cast<int>(Pick(2));
        (void)players;
        break;
    }
    case 0x0056C6E0: word(0) = Pick(5); break;                 // the cheats' count
    case 0x0056B2B0: word(0) = Next(); break;                  // (0x9A: the board parts' bits)
    case 0x0056C330: word(4) = Pick(5); break;                 // the script's object (an id)
    case 0x0056AC90: // the objects' count, the owner (a known player: the original reads its team unchecked)
        word(8) = Pick(6);
        word(4) = Pick(4);
        g_playerPresent[word(4)] = true;
        break;
    case 0x0056A000:
        if (id == 0x83) {
            real(0x30) = static_cast<float>(8 + Pick(4));
            word(0x2C) = 0xEE + Pick(4);
            if (Coin())
                g_answers[0x005532A0] = Coin() ? 0x3D6D444Fu : 0xBFF33600u; // (firefight / netfirefight)
        }
        break;
    case 0x0056C3D0:
        word(0) = Pick(3000);
        for (uint32_t at = 4; at < 0x24; at += 4)
            real(at) = static_cast<float>(static_cast<int>(Pick(2000)) - 1000) * 0.37f;
        real(0x30) = static_cast<float>(Pick(10)) * 0.5f;
        word(0x24) = Coin() ? 0xD039432Cu : 0x1234u;
        break;
    case 0x0056A450:
        if (id == 0x29)
            g_msg[1] = static_cast<uint8_t>(Pick(5));
        if (id == 0x3C)
            g_msg[0] = static_cast<uint8_t>(Next()); // (0x3C: [0] the settings' bits)
        break;
    default:
        break;
    }
}

void RecordRelay()
{
    for (uint32_t i = 0; i < 0x40; ++i)
        g_trace.push_back(reinterpret_cast<uint32_t*>(g_msg)[i]);
    for (int p = 0; p < 4; ++p) {
        auto* words = reinterpret_cast<uint32_t*>(g_players[p]);
        Record({words[0x24 / 4], words[0x34 / 4], words[0x48 / 4], words[0x1D0 / 4], words[0x1E4 / 4]});
        for (int k = 0; k < 16; ++k)
            g_trace.push_back(words[0x180 / 4 + k]);
    }
    for (int i = 0; i < kSkaters; ++i) {
        uint8_t* history = g_stateHistory[i];
        int count = *reinterpret_cast<int*>(history + 0xA6C);
        Record({static_cast<uint32_t>(count), *reinterpret_cast<uint32_t*>(history + 0x1104)});
        const auto* slot = reinterpret_cast<const uint32_t*>(history + 0xA70 + ((count + 19) % 20) * 0x54);
        for (int k = 0; k < 0x54 / 4; ++k)
            g_trace.push_back(slot[k]);
    }
    for (int k = 0; k < 0x60 / 4; ++k)
        g_trace.push_back(reinterpret_cast<uint32_t*>(g_score)[k]);
    Record({GameVariable<uint8_t>(0x007CE48C), *reinterpret_cast<uint32_t*>(g_spawned + 0xBC)});
}

void TestSkateNetRelay()
{
    RelayFixture fixture;
    const Handler kHandlers[] = {
        {0x00569E20, &SkateNet::HandleTrickObjects, {0x3B}},
        {0x00569E90, &SkateNet::HandleGameStats, {0x86}},
        {0x00569EF0, &SkateNet::HandleComboScore, {0x52}},
        {0x00569FD0, &SkateNet::HandlePlayerValue48, {0x89}},
        {0x0056A000, &SkateNet::HandleRelay, {0x4C, 0x3A, 0x89, 0x99, 0x83, 0x29}},
        {0x0056A170, &SkateNet::HandleRelayNearby, {0x2B, 0x3C, 0x35}},
        {0x0056A2D0, &SkateNet::HandleRelaySequenced, {0x48, 0x49}},
        {0x0056A3E0, &SkateNet::HandleJoined, {0x45}},
        {0x0056AD10, &SkateNet::HandleScavengerCollected, {0x90}},
        {0x0056B0B0, &SkateNet::HandleScavengerDropped, {0x8F}},
        {0x0056B2B0, &SkateNet::HandleGraffitiTexture, {0x96, 0x97, 0x98, 0x99, 0x9A}},
        {0x0056B5D0, &SkateNet::HandleFaceTexture, {0x7E}},
        {0x00569560, &SkateNet::HandleChangeLevel, {0x46}},
        {0x0056A410, &SkateNet::HandleHistoryValue, {0x89}},
        {0x0056A450, &SkateNet::HandleSkaterMessages,
         {0x29, 0x2B, 0x2A, 0x3C, 0x3D, 0x3E, 0x41, 0x2C, 0x76, 0x8C, 0x77, 0x78, 0x50}},
        {0x0056ABD0, &SkateNet::HandlePlayerLeft, {0x2D}},
        {0x0056AC50, &SkateNet::HandleServerQuit, {0x32}},
        {0x0056AC90, &SkateNet::HandleGraffitiObjects, {0x5B}},
        {0x0056B220, &SkateNet::HandlePlaceTag, {0x99}},
        {0x0056B7A0, &SkateNet::HandleSparks, {0x37, 0x38, 0x39}},
        {0x0056B7E0, &SkateNet::HandleLookUpObject, {0x39}},
        {0x0056B800, &SkateNet::HandleTerrainSound, {0x35}},
        {0x0056C330, &SkateNet::HandleRunScriptNow, {0x49}},
        {0x0056C3D0, &SkateNet::HandleProjectile, {0x83}},
        {0x0056C6E0, &SkateNet::HandleCheats, {0x82}},
        {0x0056C7E0, &SkateNet::HandleToggleCheat, {0x7F}},
        {0x0056C8C0, &SkateNet::HandleSkaterChannel, {0x2C}},
    };
    constexpr uint32_t kCount = sizeof(kHandlers) / sizeof(kHandlers[0]);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool o = run == 0;
        g_trace.clear();
        for (uint32_t seed = 0; seed < 6000; ++seed) {
            g_seed = seed * 2654435761u + 19;
            ResetWorld();
            SeedParams();
            SetUpRelay();
            Skate* skate = TheSkate();
            Record({0xEEEE0000 | seed});
            uint32_t which = seed < kCount * 150 ? seed % (kCount + 5) : Pick(kCount + 5);
            if (which < kCount) {
                const Handler& h = kHandlers[which];
                uint8_t id = h.ids[Pick(static_cast<uint32_t>(h.ids.size()))];
                ShapeMessage(h.address, id);
                Net::MsgHandlerContext context;
                std::memset(&context, 0, sizeof(context));
                context.m_Msg = reinterpret_cast<char*>(g_msg);
                context.m_MsgId = id;
                context.m_MsgLength = 1 + Pick(0x60);
                context.m_App = reinterpret_cast<Net::App*>(g_apps[1]);
                context.m_Conn = reinterpret_cast<Net::Conn*>(g_conns[Pick(3)]);
                context.m_data = skate;
                if (h.address == 0x0056AD10) { // (the original reads an unknown sender's skater: a known one)
                    int k = static_cast<int>(Pick(4));
                    g_playerPresent[k] = true;
                    context.m_Conn = reinterpret_cast<GameNet::PlayerInfo*>(g_players[k])->m_conn;
                }
                Record({h.address & 0xFFFFF, id});
                int result = o ? Original<MsgCode>(h.address)(&context) : h.ours(&context);
                Record({static_cast<uint32_t>(result)});
            } else {
                switch (which - kCount) {
                case 0:
                    if (o)
                        Original<void(__cdecl*)(void*)>(0x00569BB0)(g_relayTask);
                    else
                        SkateNet::ServerTask(reinterpret_cast<Tsk::BaseTask*>(g_relayTask));
                    break;
                case 1:
                    *reinterpret_cast<Skate**>(g_relayTask + 0x1C) = skate;
                    if (o)
                        Original<void(__cdecl*)(void*)>(0x0056BE10)(g_relayTask);
                    else
                        SkateNet::ScoreTask(reinterpret_cast<Tsk::BaseTask*>(g_relayTask));
                    break;
                case 2:
                    Call<void>(o, 0x0056B890, &Skate::LeaveServer);
                    break;
                case 3:
                    Call<void, GameNet::PlayerInfo*>(o, 0x0056B990, &Skate::SendCheats,
                                                     reinterpret_cast<GameNet::PlayerInfo*>(g_players[Pick(4)]));
                    break;
                default: {
                    bool final = Coin();
                    Call<void, bool>(o, 0x0056BB70, &Skate::SendScoreUpdates, final);
                    break;
                }
                }
            }
            RecordRelay();
            // a goal's initialised flag (0x005519F0)
            uint8_t goal[0x70] = {};
            goal[0x6C] = static_cast<uint8_t>(Next());
            auto* g = reinterpret_cast<Game::CGoal*>(goal);
            Record({static_cast<uint32_t>(o ? Original<bool(__thiscall*)(void*)>(0x005519F0)(g) : g->IsInitialized())});
        }
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 200000, "skate net relay: the original ran");
    CompareTraces(traces[0], traces[1], "skate net relay / client handlers (0x00569560-0x0056C91F)");
}

} // namespace

SELFTEST(Early, TestSkateNetRelay, RestoreSkate);
