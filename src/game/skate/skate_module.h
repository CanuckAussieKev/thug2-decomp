#pragma once

// The skate module (Mdl::Skate, 0x3F0 bytes, the singleton at 0x007CE478): the game's top-level state - its skaters
// (a list ordered by object id), the career, the rail manager, the game type, the level (requested, current and the
// one before), the competition / HORSE / movie managers, the game flow and the goal manager, the per-player settings
// and the module's own script; its logic task runs the game each frame (DoUpdate). Its code: 0x00564C10-0x005691EF
// (skate_module_state.cpp, skate_module_skaters.cpp, skate_rankings.cpp); the network side: skate_net.h.

#include <cstddef>
#include <cstdint>

#include "game/lst/head.h"
#include "game/mth/vector.h"
#include "game/sys/module.h"

namespace Obj {
class CCompositeObject;
class CObject;
class CSkater;
class CSkaterCareer;
class CRailManager;
class CMovieManager;
struct CSkaterProfile;
struct ChecksumSet;
} // namespace Obj
namespace Prefs {
class Preferences;
}
namespace Script {
class CScript;
class CStruct;
} // namespace Script
namespace Tsk {
class BaseTask;
}
namespace GameNet {
class PlayerInfo;
} // namespace GameNet
namespace Game {
class CGoalManager;
class CGameFlow;
} // namespace Game
namespace Mdl {
struct CCompetition;
class CHorse;
} // namespace Mdl

namespace Mdl {

// The skate module's virtual tables: itself, and the skater list's node and head (defined in skate_module_state.cpp).
extern void** const kSkateVtable;
extern void** const kSkaterNodeVtable;
extern void** const kSkaterHeadVtable;

class Skate : public Module {
public:
    // A skater slot's settings (+0x390, 8 of them).
    struct PlayerSettings {
        int m_unknown0;   // +0 (the player's index, set by the constructor)
        bool m_vibration; // +4 (the skater's vibration component told)
        bool m_autoKick;  // +5 (the core physics' +0x2D9)
        bool m_spinTaps;  // +6 (the core physics' +0x2DA)
        uint8_t pad7;
    };
    // A score goal (+0x60, 64 of them): a career goal done when the total first reaches `score`.
    struct ScoreGoal {
        int score;
        uint32_t script;
        int goal;
    };

    // The singleton (0x007CE478; null before the module starts). 0x005691A0 (cdecl): made (from the default heap,
    // Calloc mode 1) if there's none and `create`; one more reference either way. 0x00564C10 (cdecl): one reference
    // less - the last deletes it.
    static Skate* Instance();
    static Skate* __cdecl CreateInstance(bool create);
    static void __cdecl DeleteInstance();

    // --- made and destroyed (skate_module_skaters.cpp) ---
    // 0x00568DE0: the constructor: the module, the empty skater list, the logic task, the network tasks, the
    // managers (game flow, goals, career, rails, checksums, split-screen preferences, competition, HORSE, two movie
    // managers), the records' 21 levels, the players' settings. Returns this.
    Skate* Construct();
    // 0x005663A0: the destructor body (each manager deleted). 0x00567BA0: the scalar deleting destructor (vtable
    // slot 0).
    void DestroyBody();
    Skate* Destroy(uint8_t flags);
    // 0x00564DC0 / 0x00564C50 (vtable slots 1 / 2): the module started (the permanent assets loaded, the logic task
    // added to the main loop's) / stopped (the logic task out).
    void StartCallback();
    void StopCallback();
    // 0x00564DB0 (Tsk::Task<Skate>'s vtable slot 0): the task's code run with the task. 0x00568C90: the logic task's
    // code - DoUpdate, the skaters' collisions on the server, king of the hill's clock.
    static void __fastcall RunTask(Tsk::BaseTask* task);
    static void __cdecl LogicTask(Tsk::BaseTask* task);
    // 0x00565750 / 0x00565FC0: the skater list's node's / head's scalar deleting destructors (vtables 0x0064E544 /
    // 0x0064E5D4).
    static Mem::ListNode* __fastcall DestroySkaterNode(Mem::ListNode* node, int, uint8_t flags);
    static Mem::ListNode* __fastcall DestroySkaterHead(Mem::ListNode* head, int, uint8_t flags);

    // --- the skaters (skate_module_skaters.cpp) ---
    // 0x0041AD90: the number of skaters in the list (the nodes from the head's next until the head - checked by its
    // data, -1, before a node is checked for null, as the original does).
    int GetNumSkaters();
    // 0x005665C0: skater `index` of the list (null past the end).
    Obj::CCompositeObject* GetSkater(int index);
    // 0x00566520: the first local skater (null: none). 0x00566600: the skater whose object id is `id` (null: none).
    Obj::CSkater* GetLocalSkater();
    Obj::CSkater* GetSkaterById(uint32_t id);
    // Their entries (ECX kept, as the original's).
    static void GetLocalSkaterStub();
    static void GetSkaterByIdStub();
    // 0x00567180: a skater for `profile` (a local one: the existing local skater - given `id` when it has another -
    // or a new one on the next unused skater heap); its controllers bound, the created tricks and stat goals given
    // back. 0x00565790: a skater removed (its bail board; a client's left script; the local player's created tricks
    // and stat goals kept) and deleted.
    Obj::CSkater* AddSkater(Obj::CSkaterProfile* profile, bool local, uint32_t id, int viewport);
    void RemoveSkater(Obj::CSkater* skater);
    // 0x00565FF0: the first skater heap no skater uses (from 0 for a local skater, 1 for another).
    int GetNextUnusedSkaterHeapIndex(bool local);
    // 0x00564C60: a skater hidden or shown (`this` unused).
    void HideSkater(Obj::CSkater* skater, bool hide);
    // 0x00565BF0: a skater's shadow made again (detailed for a local skater outside split screen).
    void SetSkaterShadow(Obj::CSkater* skater);
    // 0x00567090: each local skater's (and its camera's) input bound to its player's controller.
    void BindControllers();
    // 0x00566100: the restart point for skater `skaterId` (by game mode: the team's, HORSE's, the multiplayer start
    // positions, Player1, any). 0x00566370: a skater moved to `node` (or its restart point). 0x00566980: every
    // local skater moved there and reset.
    uint32_t SkipToRestartPoint(uint32_t skaterId);
    void MoveToRestart(Obj::CSkater* skater, uint32_t node, uint32_t walking);
    void ResetSkaters(uint32_t node, uint32_t walking);
    // 0x00566BE0: the skaters' collisions (in the games that have them) and king of the hill's crown taken.
    void CheckSkaterCollisions();
    // 0x00567580: whether every skater's run is over (each one still going told to end; HORSE: only the skater
    // taking the run). 0x00567690 / 0x005676E0: HORSE's skater started / completed its first trick.
    bool AllSkatersIdle();
    bool FirstTrickStarted();
    bool FirstTrickCompleted();
    // 0x00566F40: the local skater's records of the level (high score, best combo, the longest grind / manual / lip
    // trick, the longest combo).
    void UpdateRecords();
    // 0x00567BD0 / 0x00567C00 / 0x00567C30: a player's spin taps / autokick / vibration (and its skater's).
    void SetSpinTaps(int player, bool on);
    void SetAutoKick(int player, bool on);
    void SetVibration(int player, int on);

    // --- the level (skate_module_state.cpp, skate_module_skaters.cpp) ---
    // 0x00565000: `level`'s script run (the players asked to be ready, the connections' time-outs stretched).
    // 0x00565A00: the level changed to `level` (request_level, the clean-up keeping the skaters, ChangeLevelGameFlow).
    void OpenLevel(uint32_t level);
    void ChangeLevel(uint32_t level);
    // 0x005666A0: the session's objects deleted (the skaters kept, hidden while the dead are flushed). 0x005668A0:
    // the career's level restarted. 0x00566900: the level reset (the node array, graffiti, goals, reset_level).
    // 0x00566A60: the game launched (the game flow, the king, the flags).
    void Cleanup();
    void RestartLevel();
    void ResetLevel();
    void LaunchGame();
    // 0x00564FD0: the game type loaded into the game mode.
    void SetCurrentGameType();
    // 0x00564F20: the level's objects cleaned up for the park editor.
    void CleanupForParkEditor();
    // 0x00565620: the camera's position noted for the objects' updates (none in more than one viewport).
    void PreCalculateObjectUpdateInfo();
    // 0x00567710: one frame: the time of day, the suspend frame, the movies, Game_Update, the proximity nodes, the
    // unplugged controllers.
    void DoUpdate();
    // 0x00564D30 (an object walk's callback): its suspend component's visibility.
    static void __cdecl UpdateSuspendVisibility(Obj::CObject* object, void* data);

    // --- small (skate_module_state.cpp) ---
    // 0x00564CA0: the timer's beeps (`script`) speeding up as `timeLeft` runs down from `startTime`.
    void TimerBeep(float timeLeft, float startTime, float rate, const char* script);
    // 0x00564D50 / 0x00564D60 / 0x00564D80 / 0x00564D90 / 0x00564DA0
    void SetUnknown370(int value);
    void UpdateGameFlow();
    Prefs::Preferences* GetSplitScreenPreferences();
    Game::CGoalManager* GetGoalManager();
    void CheckControllers();
    // 0x00564F60: the voices' volumes on a pause.
    void Pause(bool paused);
    // 0x00564F80: the profile `params`' integer `skater` names (none, or no params: the current one). 0x00564FC0:
    // profile `index`.
    Obj::CSkaterProfile* GetProfile(Script::CStruct* params);
    Obj::CSkaterProfile* GetProfile(int index);
    // 0x005656C0: the career's gap checklist of the level.
    void* GetGapChecklist();
    // 0x005656D0: in a net game, out of the main menu. 0x00565700: in a net mode, or the main menu / CAS asked for.
    int InNetGameLevel();
    bool InFrontEndOrNetMode();
    // 0x00565ED0: the module's script set to `name` (made if there's none). 0x00565F80: deleted (false: none).
    void RunScript(uint32_t name, Script::CStruct* params);
    bool KillScript();
    // 0x00569580 / 0x00569590: around the network manager's queued scripts.
    void BeginQueuedScripts();
    void EndQueuedScripts();
    // 0x005695A0: the cheats on, as one value (their checksums OR-ed).
    uint32_t GetCheats();
    // (skate_net_relay.cpp) 0x0056B890: out of the network game - PatchCleanup; a server tells everyone (0x32) and
    // is destroyed with its tasks, a client tells the server (0xB); the players and apps removed.
    void LeaveServer();
    // 0x0056B990: `player` told which cheats are on (0x82: a count, then the checksums).
    void SendCheats(GameNet::PlayerInfo* player);
    // 0x0056BB70: the scores to each player (0x40: the time left, the cheats, `final`, each other skater's score -
    // its own too when the player doesn't keep it itself), and to a remote player fully in the cheats' check (0x80).
    void SendScoreUpdates(bool final);

    // --- static (cdecl) ---
    // 0x005651D0: the next player observed. 0x005651F0: a game of more than one player (1 / 0). 0x00565680: the
    // network / taunt / split-screen preferences. 0x00565D40: those `params`' prefs names. 0x00565BB0: an object
    // AbsentInNetGames in one (1 / 0). 0x00565DA0: the rails' debug drawing (compiled out).
    static void __cdecl ObserveNextSkater();
    static int __cdecl IsMultiplayer();
    static Prefs::Preferences* __cdecl GetPreferences(uint32_t name);
    static Prefs::Preferences* __cdecl GetPreferencesFromParams(Script::CStruct* params);
    static int __stdcall ShouldBeAbsent(Script::CStruct* params);
    static void __cdecl DebugRails();

    // --- script commands ---
    // 0x00565220 SetGameType, 0x00565250 InTeamGame, 0x00565260 ChangeLevel (the players told; the park's map /
    // rails and created goals streamed to them), 0x005654B0 LaunchLevel, 0x005654D0 RequestLevel, 0x005655B0
    // InitSkaterHeaps, 0x00566E70 Retry, 0x00566EA0 LaunchGame, 0x00566ED0 ResetLevel, 0x00568120
    // FillRankingScreen (skate_rankings.cpp).
    static bool __cdecl CFunc_SetGameType(Script::CStruct* params, Script::CScript* script);
    static int __cdecl CFunc_InTeamGame(Script::CStruct* params, Script::CScript* script);
    static bool __cdecl CFunc_ChangeLevel(Script::CStruct* params, Script::CScript* script);
    static bool __cdecl CFunc_LaunchLevel(Script::CStruct* params, Script::CScript* script);
    static bool __cdecl CFunc_RequestLevel(Script::CStruct* params, Script::CScript* script);
    static bool __cdecl CFunc_InitSkaterHeaps(Script::CStruct* params, Script::CScript* script);
    static bool __cdecl CFunc_Retry(Script::CStruct* params, Script::CScript* script);
    static bool __cdecl CFunc_LaunchGame(Script::CStruct* params, Script::CScript* script);
    static bool __cdecl CFunc_ResetLevel(Script::CStruct* params, Script::CScript* script);
    static bool __cdecl CFunc_FillRankingScreen(Script::CStruct* params, Script::CScript* script);

    // (the list's node at `index` - Lst::Head's GetItem; the skater there, null past the end)
    Mem::ListNode* SkaterNode(uint32_t index);
    Obj::CSkater* SkaterAt(uint32_t index);

    Tsk::BaseTask* m_logicTask;                   // +0x14 (Tsk::Task<Skate>, vtable 0x006476F0: LogicTask)
    Tsk::BaseTask* m_serverTasks[2];              // +0x18 (SkateNet::CreateNetworkTasks)
    Obj::CSkaterCareer* m_career;                 // +0x20
    Obj::CRailManager* m_railManager;             // +0x24
    Lst::Head m_skaters;                          // +0x28 (their nodes' data the skaters, in object id order)
    bool m_checkControllers;                      // +0x3C
    uint8_t pad3D[3];
    int m_unpluggedFrames;                        // +0x40
    bool m_runningQueuedScripts;                  // +0x44
    bool m_levelLaunched;                         // +0x45 (OpenLevel)
    bool m_endRun;                                // +0x46
    uint8_t pad47[5];
    uint32_t m_gameType;                          // +0x4C (freeskate)
    bool m_movieCamera;                           // +0x50 a movie's camera in use
    uint8_t pad51[3];
    Obj::ChecksumSet* m_checksums;                // +0x54 (0x404 bytes)
    Prefs::Preferences* m_splitscreenPreferences; // +0x58
    bool m_levelChanged;                          // +0x5C
    uint8_t pad5D[3];
    ScoreGoal m_scoreGoals[64];                   // +0x60
    Mdl::CCompetition* m_competition;             // +0x360
    Mdl::CHorse* m_horse;                         // +0x364
    Obj::CMovieManager* m_movies;                 // +0x368
    Obj::CMovieManager* m_objectMovies;           // +0x36C
    int m_unknown370;                             // +0x370
    uint32_t m_requestedLevel;                    // +0x374
    uint32_t m_level;                             // +0x378
    uint32_t m_previousLevel;                     // +0x37C
    uint32_t m_unknown380;                        // +0x380
    bool m_newRecord;                             // +0x384 a record was set by the last UpdateRecords
    uint8_t pad385[3];
    Game::CGameFlow* m_gameFlow;                  // +0x388
    Game::CGoalManager* m_goalManager;            // +0x38C
    PlayerSettings m_players[8];                  // +0x390
    int m_unknown3D0;                             // +0x3D0
    bool m_debugRails;                            // +0x3D4
    uint8_t pad3D5[3];
    Script::CScript* m_script;                    // +0x3D8
    bool m_noCamera;                              // +0x3DC (PreCalculateObjectUpdateInfo: no camera to measure from)
    uint8_t pad3DD[3];
    Mth::Vector m_cameraPos;                      // +0x3E0
};
static_assert(offsetof(Skate, m_logicTask) == 0x14 && offsetof(Skate, m_skaters) == 0x28 &&
                  offsetof(Skate, m_gameType) == 0x4C && offsetof(Skate, m_scoreGoals) == 0x60 &&
                  offsetof(Skate, m_competition) == 0x360 && offsetof(Skate, m_gameFlow) == 0x388 &&
                  offsetof(Skate, m_players) == 0x390 && offsetof(Skate, m_script) == 0x3D8 &&
                  offsetof(Skate, m_cameraPos) == 0x3E0 && sizeof(Skate) == 0x3F0,
              "layout must match the original");

// 0x00567C70: elimiskate's rankings (FillRankingScreen's: by the time each player was knocked out).
void __cdecl FillEliminationRanking();

// 0x00565730: the skater list head's destructor body (its node's: unlinked).
void __fastcall SkaterHeadDestroyBody(Mem::ListNode* head);

extern int g_viewportCount;
extern Mdl::Skate* g_skateModule;

} // namespace Mdl
