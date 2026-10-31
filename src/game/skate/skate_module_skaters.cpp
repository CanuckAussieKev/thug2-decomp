// The skate module's skaters (see skate_module.h): the list walked by index, added / removed / re-identified, sent to
// their restart points, cleaned up and reset with the level, the game launched, their collisions, runs and records,
// the per-player settings, the module's script, its destructor.

#include "game/skate/skate_module.h"
#include "game/net/task_vtable.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/core/task_list.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/front_end.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/gfx/tod_manager.h"
#include "game/pad_input/device.h"
#include "game/pad_input/input_hub.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/player_info.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/emitter_object.h"
#include "game/obj/movie_manager.h"
#include "game/obj/entity_hub.h"
#include "game/obj/proximity.h"
#include "game/obj/suspend_component.h"
#include "game/obj/shadow_component.h"
#include "game/obj/trick_objects.h"
#include "game/obj/vibration_component.h"
#include "game/script/preferences.h"
#include "game/skate/highscores.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/skate/career.h"
#include "game/skate/contest.h"
#include "game/skate/game_mode.h"
#include "game/skate/letter_match.h"
#include "game/skate/profile_manager.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_net.h"
#include "game/skater/end_run.h"
#include "game/obj/input_component.h"
#include "game/obj/node_components.h"
#include "game/obj/rail_manager.h"
#include "game/skater/rider.h"
#include "game/skater/state_history.h"
#include "game/skater/trick_component.h"
#include "game/sound/surface_kind.h"
#include "game/sys/stopwatch.h"
#include "game/sys/model_viewer.h"
#include "game/gfx/anim_cache.h"
#include "game/gfx/engine.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/gfx/model_appearance.h"
#include "game/gfx/particle_sets.h"
#include "game/gfx/viewport.h"
#include "game/obj/particle_component.h"
#include "game/gfx/settings.h"

namespace GameModules {

int g_viewportCount;
Scripts::CStruct* g_catInfo;
Scripts::CStruct* g_statTargetInfo;

namespace {

constexpr uint32_t kBailBoard = 0x884EF81B;          // BailBoard (+ the skater's id)
constexpr uint32_t kSkaterId = 0x5B24FAAA;           // SkaterId
constexpr uint32_t kRemoveClientSkater = 0x5B3FDCCB; // (name unknown: a client's skater left)
constexpr uint32_t kLevel = 0x651533EC;              // level
constexpr uint32_t kRequestLevel = 0x38028FC9;       // request_level
constexpr uint32_t kLoadMainMenu = 0xE92ECAFE;       // Load_MainMenu
constexpr uint32_t kPreserveSkaters = 0x7F07D3D4;    // preserve_skaters
constexpr uint32_t kChangeLevelGameFlow = 0xFF135E2A; // ChangeLevelGameFlow
constexpr uint32_t kShadow = 0x8A897DD2;             // shadow
constexpr uint32_t kShadowScale = 0x6F8CD62F;        // (name unknown: the shadow's scale)
constexpr uint32_t kShadowModel = 0x545F8172;        // (name unknown: the shadow's model)
constexpr uint32_t kRailManagerType = 0x1CDFB2A3;    // railmanager
constexpr uint32_t kTeam = 0x3B1F59E0;               // Team
constexpr uint32_t kNetLobby = 0x1C471C60;           // NetLobby
constexpr uint32_t kNetCtf = 0x6C5FF266;             // netctf
constexpr uint32_t kNetScavengerHunt = 0xFC13B531;   // netscavengerhunt
constexpr uint32_t kScavengerHunt = 0x846504FC;      // scavengerhunt
constexpr uint32_t kScavenger = 0x1F3430CE;          // scavenger
constexpr uint32_t kIsHorse = 0xD1993397;            // is_horse
constexpr uint32_t kHorse = 0x9D65D0E7;              // Horse
constexpr uint32_t kMultiplayer = 0xBAE0E4DA;        // Multiplayer
constexpr uint32_t kPlayer1 = 0x41639CE5;            // Player1
constexpr uint32_t kUninitLightManager = 0x03732450; // uninit_light_manager
constexpr uint32_t kResetLevel = 0xEDEA4ECE;         // reset_level
constexpr uint32_t kInitializeGameFlow = 0xBC945B1B; // InitializeGameFlow
constexpr uint32_t kStandardGameFlow = 0xA4E32C9B;   // StandardGameFlow
constexpr uint32_t kNetFirefight = 0xBFF33600;       // netfirefight
constexpr uint32_t kFirefight = 0x3D6D444F;          // firefight
constexpr uint32_t kNetKing = 0x6EF8FDA0;            // netking
constexpr uint32_t kKing = 0x5D32129C;               // king
constexpr uint32_t kSkaterBalanceTrick = 0x589E5FB3; // SkaterBalanceTrick
constexpr uint32_t kInput = 0x27D7CD28;              // input
constexpr uint32_t kShowPanelStuff = 0xD97CE49F;     // show_panel_stuff
constexpr uint32_t kDestroySkaterParticles = 0xF429E0AC; // DestroySkaterParticles
constexpr uint32_t kInitSkaterParticles = 0xAE5438AF;    // InitSkaterParticles
constexpr uint32_t kSkaterCam0 = 0x967C138C;         // skatercam0
constexpr uint32_t kVibration = 0x404EB936;          // vibration
constexpr uint32_t kPlayer = 0x67E6859A;             // player
constexpr uint32_t kModelApplyCustomLogos = 0x27670630; // model_apply_custom_logos
constexpr uint32_t kClientSkaterInit = 0x41FC854A;   // ClientSkaterInit
constexpr uint32_t kTrick = 0x270F56E1;              // trick
constexpr uint32_t kSkaterEndRun = 0xB1D5A3E1;       // SkaterEndRun
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;  // SkaterCorePhysics
constexpr uint32_t kFreeskate = 0x34861A16;           // freeskate
constexpr uint32_t kDefaultSplitscreenPreferences = 0x0076EAE3; // default_splitscreen_preferences
constexpr uint8_t kMsgKing = 0x55;                   // the new king of the hill (its id)
constexpr uint8_t kGroupGame = 8;
// The CTF / scavenger hunt restart points by team.
constexpr uint32_t kTeamRestarts[4] = {0xACD3422F, 0x9AB434CC, 0x5C427A5C, 0x2C3ED607}; // TRG_CTF_Restart_<colour>

GoalSystem::CMatchRules* GameMode() { return GoalSystem::g_gameMode; }
OnlinePlay::Hub* NetSessionSlot() { return OnlinePlay::g_onlineHub; }
GameObjects::CMiscHub* EntityHubHandle() { return reinterpret_cast<GameObjects::CMiscHub*>(GameObjects::g_compositeRegistry); }
GameObjects::CTrickSpotHub* TrickObjects() { return GameObjects::g_trickSpotHub; }
Scripts::CStruct*& CatInfo() { return g_catInfo; }
Scripts::CStruct*& StatGoalParamsRef() { return g_statTargetInfo; }
// The C runtime's _ftol (0x006236FC): truncated toward zero, the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
MemSys::Arena* RiderHeapAt(int index) { return MemSys::Hub::Pull()->m_skaterHeaps[index]; }

struct PlayerSearch {
    void** m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};
OnlinePlay::PlayerRecord* AsPlayer(void* player) { return static_cast<OnlinePlay::PlayerRecord*>(player); }

Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* made = Scripts::SpawnParamBag();
    return made ? made->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

// The skater list's node (ListKit::Elem<CRider>, +0x238 of the skater) out of the list.
void UnlinkSkaterNode(MemSys::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// --- the list -------------------------------------------------------------------------------------------------------

MemSys::ListNode* Skate::SkaterNode(uint32_t index)
{
    return reinterpret_cast<GoalSystem::GoalListHead*>(&m_riders)->NodeAt(static_cast<int>(index)); // (ListKit::Foremost's FetchEntry)
}

// 0x005665C0
GameObjects::CGameEntity* Skate::GetRiderAt(int index)
{
    if (static_cast<uint32_t>(CountRiders()) <= static_cast<uint32_t>(index))
        return nullptr;
    return static_cast<GameObjects::CGameEntity*>(SkaterNode(static_cast<uint32_t>(index))->data);
}

// The skater at `index` as the module's loops find it (null past the end; the node's data otherwise).
GameObjects::CRider* Skate::SkaterAt(uint32_t index)
{
    if (static_cast<uint32_t>(CountRiders()) <= index)
        return nullptr;
    return static_cast<GameObjects::CRider*>(SkaterNode(index)->data);
}

// 0x00566520
GameObjects::CRider* Skate::GetHomeRider()
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(CountRiders()); ++i) {
        auto* skater = static_cast<GameObjects::CRider*>(SkaterNode(i)->data); // (no check: as the original)
        if (skater->m_unknownBC)
            return skater;
    }
    return nullptr;
}

// 0x00566600
GameObjects::CRider* Skate::GetRiderByNumber(uint32_t id)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(CountRiders()); ++i) {
        auto* skater = static_cast<GameObjects::CRider*>(SkaterNode(i)->data);
        if (skater->m_ident == id)
            return skater;
    }
    return nullptr;
}

// The two lookups keep ECX (the original's leave it alone, and the whole-program-optimised callers rely on it: RunFrameLogic
// calls GetHomeRider twice with the module in ECX).
namespace {
GameObjects::CRider* __fastcall LocalSkaterOf(Skate* skate) { return skate->GetHomeRider(); }
GameObjects::CRider* __fastcall SkaterByIdOf(Skate* skate, int, uint32_t id) { return skate->GetRiderByNumber(id); }
} // namespace

__declspec(naked) void Skate::GetLocalSkaterStub()
{
    __asm {
        push ecx
        call LocalSkaterOf
        pop ecx
        ret
    }
}

__declspec(naked) void Skate::GetSkaterByIdStub()
{
    __asm {
        push ecx
        push dword ptr [esp + 8]
        call SkaterByIdOf
        pop ecx
        ret 4
    }
}

// --- adding and removing --------------------------------------------------------------------------------------------

// 0x00565790
void Skate::DropSkaterFromRoster(GameObjects::CRider* skater)
{
    if (GameObjects::CEntity* board = EntityHubHandle()->LookUpEntity(skater->m_ident + kBailBoard)) {
        reinterpret_cast<void(__thiscall*)(GameObjects::CEntity*)>(board->m_vtable[0x18 / 4])(board); // (marked dead)
        EntityHubHandle()->FlushDeadObjects();
    }
    if (!skater->m_unknownBC) {
        Scripts::CStruct* params = SpawnParamTable();
        params->PutNameKey(kSkaterId, skater->m_ident);
        Scripts::LaunchScript(kRemoveClientSkater, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    if (skater->m_unknownBC && skater->m_viewSlot == 0) {
        // The local player's created tricks and stat goals kept for the skater that replaces it.
        if (CatInfo()) {
            DeleteStructure(CatInfo());
            CatInfo() = nullptr;
        }
        DebugOut::Printf("CAT: Storing Created Trick data\n"); // "CAT: Storing Created Trick data\n"
        CatInfo() = SpawnParamTable();
        skater->WriteCATInfo(CatInfo());
        if (StatGoalParamsRef()) {
            DeleteStructure(StatGoalParamsRef());
            StatGoalParamsRef() = nullptr;
        }
        DebugOut::Printf("CAT: Storing Stat Goal data\n"); // "CAT: Storing Stat Goal data\n"
        StatGoalParamsRef() = SpawnParamTable();
        skater->WriteList44B66A9F(StatGoalParamsRef());
    }
    UnlinkSkaterNode(static_cast<MemSys::ListNode*>(skater->GetBlock238()));
    reinterpret_cast<void(__thiscall*)(GameObjects::CRider*, int)>(skater->m_vtable[0])(skater, 1);
}

// 0x00567180
GameObjects::CRider* Skate::SpawnSkaterEntity(GameObjects::CRiderIdentity* profile, bool local, uint32_t id, int viewport)
{
    OnlinePlay::Hub* net = NetSessionSlot();
    if (local) {
        GameObjects::CRiderIdentity* source = GameObjects::g_profileRoster->FetchAccount(
            static_cast<uint8_t>(ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr)) ? viewport : 0);
        reinterpret_cast<Gfx2D::CModelLook*>(profile)->CopyFrom(reinterpret_cast<Gfx2D::CModelLook*>(source));
        Scripts::LaunchScript(kShowPanelStuff, nullptr, nullptr, false, nullptr);
        GameObjects::CRider* skater = GetHomeRider();
        if (skater) {
            if (static_cast<uint8_t>(ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr))) {
                if (skater->m_ident == id)
                    return skater;
            } else {
                if (skater->m_ident == id)
                    return skater;
                // The local skater given the new id (its place in the list by it, its particles, its score's).
                auto* node = static_cast<MemSys::ListNode*>(skater->GetBlock238());
                node->priority = static_cast<int>(id);
                UnlinkSkaterNode(node);
                reinterpret_cast<ListKit::Foremost*>(&m_riders)->InsertByUniqueOrder(node);
                MemSys::EnterHeapScope(RiderHeapAt(skater->GetSkaterNumber()));
                Scripts::LaunchScript(kDestroySkaterParticles, nullptr, skater, false, nullptr);
                skater->ChangeSerial(id);
                Scripts::LaunchScript(kInitSkaterParticles, nullptr, skater, false, nullptr);
                MemSys::LeaveHeapScope();
                static_cast<PointTally*>(skater->GetPointsTotal())->SetRiderNumber(static_cast<int16_t>(id));
                return skater;
            }
        } else if (GameObjects::CEntity* camera = EntityHubHandle()->LookUpEntity(kSkaterCam0)) {
            camera->m_objectFlags &= ~2u;
            reinterpret_cast<void(__thiscall*)(GameObjects::CEntity*, int)>(camera->m_vtable[0])(camera, 1);
        }
    } else if (net->InNetGame()) {
        if (GameObjects::CRider* skater = GetHomeRider()) {
            if (auto* vibration = static_cast<GameObjects::CPadRumbleModule*>(skater->FetchModule(kVibration)))
                vibration->StopAll();
        }
    }
    net->InNetGame(); // (its answer unused, as the original)
    MemSys::PushMemProfile("Skater"); // "Skater"
    int heap = FindFreeRiderHeap(local);
    MemSys::EnterHeapScope(RiderHeapAt(heap));
    void* memory = MemSys::AllocZeroed(sizeof(GameObjects::CRider));
    GameObjects::CRider* skater = memory ? static_cast<GameObjects::CRider*>(memory)->MakeNew(
                                        viewport, reinterpret_cast<GameObjects::CEntityHub*>(EntityHubHandle()),
                                        local, id, heap, 1)
                                  : nullptr;
    reinterpret_cast<void(__thiscall*)(GameObjects::CRider*, GameObjects::CRiderIdentity*)>(skater->m_vtable[0x34 / 4])(skater, profile);
    if (static_cast<uint8_t>(ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr))) {
        Scripts::CStruct* params = SpawnParamTable();
        params->PutInt(kPlayer, 0);
        Scripts::LaunchScript(kModelApplyCustomLogos, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    if (!skater->m_unknownBC)
        Scripts::LaunchScript(kClientSkaterInit, nullptr, skater, false, nullptr);
    auto* node = static_cast<MemSys::ListNode*>(skater->GetBlock238());
    node->priority = static_cast<int>(skater->m_ident);
    reinterpret_cast<ListKit::Foremost*>(&m_riders)->InsertByUniqueOrder(node);
    BindControllers();
    if (skater->m_unknownBC && skater->m_viewSlot == 0) {
        if (CatInfo()) {
            DebugOut::Printf("CAT: Restoring Created Tricks\n"); // "CAT: Restoring Created Tricks\n"
            skater->LoadCATInfo(CatInfo());
            DeleteStructure(CatInfo());
            CatInfo() = nullptr;
        }
        if (StatGoalParamsRef()) {
            DebugOut::Printf("CAT: Restoring Stat Goal data\n"); // "CAT: Restoring Stat Goal data\n"
            skater->LoadList44B66A9F(StatGoalParamsRef());
            DeleteStructure(StatGoalParamsRef());
            StatGoalParamsRef() = nullptr;
        }
    }
    if (skater->m_ident == 1 && static_cast<uint8_t>(ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr)))
        static_cast<GameObjects::CMoveExecutor*>(skater->FetchModule(kTrick))->ApplyTrickBindings(profile);
    MemSys::PopMemProfile();
    MemSys::LeaveHeapScope();
    return skater;
}

// 0x00565FF0
int Skate::FindFreeRiderHeap(bool local)
{
    int index = local ? 0 : 1;
    if (GameMode()->GetRiderCeiling() <= 0)
        return index;
    for (int tries = 0;;) {
        bool used = false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(CountRiders()); ++i) {
            if (static_cast<GameObjects::CRider*>(SkaterNode(i)->data)->GetSkaterNumber() == index) {
                used = true;
                break;
            }
        }
        if (!used)
            return index;
        index = static_cast<int>(static_cast<uint32_t>(index + 1) %
                                 static_cast<uint32_t>(GameMode()->GetRiderCeiling()));
        if (++tries >= GameMode()->GetRiderCeiling())
            return index;
    }
}

// 0x00565BF0
void Skate::SetSkaterShadow(GameObjects::CRider* skater)
{
    auto* shadow = static_cast<GameObjects::CGroundShadowModule*>(skater->FetchModule(kShadow));
    int type = Engine3D::g_hqShadows ? 1 : 2;
    MemSys::EnterHeapScope(RiderHeapAt(skater->GetSkaterNumber()));
    if (static_cast<uint8_t>(ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr)))
        type = 2;
    if (!skater->m_unknownBC)
        type = 2;
    Scripts::CStruct* params = SpawnParamTable();
    params->PushFloat(kShadowScale, 1.0f);
    params->AppendWord(kShadowModel, "Ped_Shadow"); // "Ped_Shadow"
    reinterpret_cast<void(__thiscall*)(GameObjects::CGroundShadowModule*, Scripts::CStruct*)>(shadow->m_vtable[2])(shadow, params);
    DeleteStructure(params);
    shadow->DestroyShadow();
    shadow->BuildBlobShadow(type, "Ped_Shadow");
    MemSys::LeaveHeapScope();
}

// 0x00567090
void Skate::BindControllers()
{
    for (uint32_t i = 0; i < 8; ++i) {
        GameObjects::CRider* skater = SkaterAt(i);
        if (!skater || !skater->m_unknownBC)
            continue;
        int number = skater->GetSkaterNumber();
        auto* input = PadInput::g_inputManager;
        int controller = input->GetPlayerController(number);
        static_cast<GameObjects::CPadInputModule*>(skater->FetchModule(kInput))->LinkToPad(controller);
        if (auto* camera = static_cast<GameObjects::CGameEntity*>(skater->GetSkaterCamera())) {
            controller = PadInput::g_inputManager->GetPlayerController(number);
            static_cast<GameObjects::CPadInputModule*>(camera->FetchModule(kInput))->LinkToPad(controller);
        }
    }
}

// --- the restart points ---------------------------------------------------------------------------------------------

// 0x00566100
uint32_t Skate::WarpToRestart(uint32_t skaterId)
{
    OnlinePlay::Hub* net = NetSessionSlot();
    auto generic = [] {
        DebugOut::Printf("\n\nWARNING - no Player1 restart point, trying Generic\n\n"); // "WARNING - no Player1 restart point, trying Generic"
        uint32_t node = GameObjects::GetSpawnNode(0, 0, 0);
        if (!node)
            DebugOut::Printf("\n\nWARNING - No restart points found\n\n"); // "WARNING - No restart points found"
        return node;
    };
    auto found = [&](uint32_t node) { return node ? node : generic(); };
    auto multiplayer = [&](uint32_t start) {
        uint32_t node = GameObjects::GetSpawnNode(kMultiplayer, static_cast<int>(net->GetStartInfoWord(static_cast<int>(start))), 0);
        if (node)
            return node;
        DebugOut::Printf("\n\nWARNING - no multiplayer restart point, trying player1\n\n"); // "WARNING - no multiplayer restart point, trying player1"
        return found(GameObjects::GetSpawnNode(kPlayer1, 0, 0));
    };
    if (!net->InNetGame() && GameMode()->GetStartingRiderCount() <= 1)
        return found(GameObjects::GetSpawnNode(kPlayer1, static_cast<int>(skaterId), 0));
    if (static_cast<uint8_t>(GameMode()->IsSquadMatch())) {
        if (GameMode()->GetNameKey() == kNetLobby)
            return found(GameObjects::GetSpawnNode(kTeam, 0, 0));
        if (GameMode()->GetNameKey() == kNetCtf) {
            auto* local = AsPlayer(net->GetOwnPlayer());
            if (!local || static_cast<uint32_t>(local->m_team) > 3)
                return generic();
            return kTeamRestarts[local->m_team];
        }
        if (GameMode()->GetNameKey() == kNetScavengerHunt) {
            auto* local = AsPlayer(net->GetOwnPlayer());
            if (!local)
                return generic();
            GoalSystem::CObjective* hunt = GoalSystem::GetObjectiveBoard()->GetGoal(kScavenger, 1);
            int team = GoalSystem::Virtual<bool>(hunt, GoalSystem::CObjective::kVtIsActive) ? local->m_team
                                                                             : (local->m_team + 1) % GameMode()->NumTeams();
            if (static_cast<uint32_t>(team) > 3)
                return generic();
            return kTeamRestarts[team];
        }
        return multiplayer(skaterId);
    }
    if (GameMode()->IsTrue(kIsHorse))
        return found(GameObjects::GetSpawnNode(kHorse, m_horse->GetRestart(), 0));
    uint32_t start = skaterId;
    if (GameMode()->GetNameKey() == kNetScavengerHunt || GameMode()->GetNameKey() == kScavengerHunt) {
        GoalSystem::CObjective* hunt = GoalSystem::GetObjectiveBoard()->GetGoal(kScavenger, 1);
        if (!GoalSystem::Virtual<bool>(hunt, GoalSystem::CObjective::kVtIsActive))
            start = (skaterId + 1) % static_cast<uint32_t>(CountRiders());
    }
    return multiplayer(start);
}

// 0x00566370
void Skate::TeleportToSpawn(GameObjects::CRider* skater, uint32_t node, uint32_t walking)
{
    if (!node) {
        node = WarpToRestart(skater->m_ident);
        if (!node)
            return;
    }
    reinterpret_cast<void(__thiscall*)(GameObjects::CRider*, uint32_t, uint32_t)>(skater->m_vtable[0x3C / 4])(skater, node,
                                                                                                          walking);
}

// 0x00566980
void Skate::ResetSkaters(uint32_t node, uint32_t walking)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(CountRiders()); ++i) {
        GameObjects::CRider* skater = SkaterAt(i); // (no check: as the original)
        if (skater->m_unknownBC) {
            uint32_t at = node ? node : WarpToRestart(skater->m_ident);
            if (at)
                reinterpret_cast<void(__thiscall*)(GameObjects::CRider*, uint32_t, uint32_t)>(skater->m_vtable[0x3C / 4])(
                    skater, at, walking);
        }
        skater->Reset27C();
    }
    m_runOver = false;
}

} // namespace Mdl

namespace GameModules {

// --- the level's session --------------------------------------------------------------------------------------------

// 0x005666A0
void Skate::TidyUp()
{
    DebugOut::Printf("Skate::Cleanup() - Deleting all session specific objects\n"); // "Skate::TidyUp() - Deleting all session specific objects\n"
    for (uint32_t i = 0; i < static_cast<uint32_t>(CountRiders()); ++i) {
        GameObjects::CRider* skater = SkaterAt(i);
        if (!skater)
            continue;
        skater->m_objectFlags |= 2; // (kept through the flush below)
        if (skater->m_unknownBC) {
            skater->Relaunch(0, 1);
            m_movies->ClearMovies();
        }
        skater->ResetComponents();
        skater->ConcealSkater();
    }
    m_movies->ClearMovies();
    m_objectMovies->StopAll();
    m_objectMovies->ClearMovies();
    if (Viewer* viewer = GetLiveViewer())
        viewer->DropViewerEntity();
    EntityHubHandle()->ForEach(&GameObjects::DisposeWhenFree, nullptr);
    EntityHubHandle()->FlushDeadObjects();
    for (uint32_t i = 0; i < static_cast<uint32_t>(CountRiders()); ++i) {
        GameObjects::CRider* skater = SkaterAt(i);
        if (!skater)
            continue;
        skater->m_objectFlags &= ~2u;
        skater->ShowSkater();
    }
    NetSessionSlot()->ClearCrown();
    Scripts::KillAllChildScripts();
    TrickObjects()->RemoveAllClusters();
    static_cast<Engine3D::CFlexibleParticleManager*>(Engine3D::g_flexibleParticleManager)->DestroyNonPersistent();
    m_railManager->TidyUp();
    GameObjects::NearbyZone_Shutdown();
    GameObjects::Emitter_Cleanup();
    static_cast<Engine3D::CEffectsDirector*>(static_cast<void*>(GameObjects::g_particleHub))->KillEveryParticle();
}

// 0x00565A00
void Skate::ChangeLevel(uint32_t level)
{
    DebugOut::Printf("-----------------------------------------Skate::ChangeLevel(%d)\n", level); // "-----...Skate::ChangeLevel(%d)\n"
    MemSys::EnterHeapScope(MemSys::Hub::Pull()->m_skaterInfoHeap);
    Scripts::CStruct* params = SpawnParamTable();
    params->Clear();
    params->AddComponentOfType(kLevel, Scripts::QBVAL_NAME, level);
    Scripts::LaunchScript(kRequestLevel, params, nullptr, false, nullptr);
    DeleteStructure(params);
    m_previousLevel = m_level;
    m_level = level;
    params = SpawnParamTable();
    params->Clear();
    if (level != kLoadMainMenu)
        params->AddComponentOfType(0, Scripts::QBVAL_NAME, kPreserveSkaters);
    ScriptCommands::CFunc_Cleanup(params, nullptr);
    DeleteStructure(params);
    m_stageSwitched = true;
    m_gameFlow->Request(kChangeLevelGameFlow);
    MemSys::LeaveHeapScope();
}

// 0x005668A0
void Skate::RestartLevel()
{
    m_runOver = false;
    if ((CountRiders() || NetSessionSlot()->InNetGame()) && m_levelLaunched) {
        m_career->BeginLevel(-1);
        Stubs::DoNothing(); // (a compiled-out call on the trick object manager)
    }
}

// 0x00566900
void Skate::ResetLevel()
{
    OnlinePlay::Hub* net = NetSessionSlot();
    TidyUp();
    ScriptCommands::CFunc_ParseNodeArray(nullptr, nullptr);
    m_runOver = false;
    TrickObjects()->ApplyPlayerGraffiti();
    GoalSystem::CGoalManager* goals = GoalSystem::GetObjectiveBoard();
    if (net->InNetGame())
        goals->RevokeEveryClear();
    else
        goals->PrepareEveryObjective();
    goals->SpawnObjectiveProps();
    ScriptCommands::g_todManager->ClearDeadObjects();
    Scripts::LaunchScript(kResetLevel, nullptr, nullptr, false, nullptr);
}

// 0x00566A60
void Skate::LaunchGame()
{
    OnlinePlay::Hub* net = NetSessionSlot();
    g_frontEndInstance->PauseGame(false);
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    DebugOut::Printf("\nLAUNCH GAME CALLED with %d skaters \n\n", CountRiders()); // "\nLAUNCH GAME CALLED with %d skaters \n\n"
    if (!net->GetOwnPlayer()) {
        if (static_cast<uint32_t>(GameMode()->GetRiderCeiling()) > 1)
            MemSys::Hub::Pull()->InitExtraSkaterHeaps(GameMode()->GetRiderCeiling());
        m_gameFlow->Request(kInitializeGameFlow);
        m_gameFlow->TickFrame();
    } else {
        ResetLevel();
        m_gameFlow->Request(kStandardGameFlow);
    }
    PlayerSearch search;
    if (auto* king = AsPlayer(net->GetHillRuler()))
        king->GiveCrown(false);
    for (void* player = net->BeginPlayerWalk(&search, true); player; player = net->StepPlayerWalk(&search, true))
        AsPlayer(player)->ClearFlags();
    if (GameMode()->GetNameKey() == kNetLobby)
        net->BuildTeamFlagSet(GameMode()->NumTeams());
    MemSys::LeaveHeapScope();
}

// 0x00566BE0
void Skate::TestRiderContacts()
{
    OnlinePlay::Hub* net = NetSessionSlot();
    if (net->HasMatchEnded())
        return;
    if (!net->PlayersCanCollide() && GameMode()->GetNameKey() != kNetFirefight &&
        GameMode()->GetNameKey() != kFirefight && GameMode()->GetNameKey() != kNetKing &&
        GameMode()->GetNameKey() != kKing && GameMode()->GetNameKey() != kNetCtf)
        return;
    for (uint32_t i = 0; i < 8; ++i) {
        if (GameObjects::CRider* skater = SkaterAt(i))
            reinterpret_cast<GameObjects::CRiderFrameLog*>(skater->m_unknown27C)->CheckForCollisions(0);
    }
    if (GameMode()->GetNameKey() != kNetKing && GameMode()->GetNameKey() != kKing)
        return;
    auto* crown = static_cast<GameObjects::CCrownObject*>(net->GetCrownHolder());
    if (!crown || crown->IsWorn())
        return;
    // The crown to the first skater near the local one.
    GameObjects::CRider* king = nullptr;
    for (uint32_t i = 0; i < 8 && !king; ++i) {
        GameObjects::CRider* skater = SkaterAt(i);
        if (skater && reinterpret_cast<GameObjects::CRiderFrameLog*>(skater->m_unknown27C)->IsNearLocalSkater())
            king = skater;
    }
    if (!king)
        return;
    PlayerSearch search;
    AsPlayer(net->FindPlayerSlotByObj(static_cast<uint16_t>(king->m_ident)))->GiveCrown(true);
    for (void* player = net->BeginPlayerWalk(&search, true); player; player = net->StepPlayerWalk(&search, true)) {
        if (static_cast<uint8_t>(AsPlayer(player)->IsOwnPlayer()))
            continue;
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        NetLink::AppHost* server = net->m_host; // (0x005480C0)
        uint8_t id = static_cast<uint8_t>(king->m_ident);
        desc.m_content = &id;
        desc.m_byteCount = 1;
        desc.m_ident = kMsgKing;
        desc.m_queue = NetLink::NetMessageInfo::kLaneOrdered;
        desc.m_groupId = kGroupGame;
        server->PutOnOutbox(AsPlayer(player)->GetLinkHandle(), &desc);
    }
}

// 0x00566E70
bool __cdecl Skate::CFunc_Retry(Scripts::CStruct*, Scripts::CScript*)
{
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    Sole()->LaunchGame();
    MemSys::LeaveHeapScope();
    return true;
}

// 0x00566EA0
bool __cdecl Skate::CFunc_LaunchGame(Scripts::CStruct*, Scripts::CScript*)
{
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    Sole()->LaunchGame();
    MemSys::LeaveHeapScope();
    return true;
}

// 0x00566ED0
bool __cdecl Skate::CFunc_ResetLevel(Scripts::CStruct*, Scripts::CScript* script)
{
    // (the calling script isn't spawned while the level resets: its flag put back after)
    uint8_t spawned = 0;
    Skate* skate = Sole();
    if (script) {
        spawned = script->m_spawnFlags & Scripts::SCRIPT_FLAG_SPAWNED;
        script->m_spawnFlags &= ~Scripts::SCRIPT_FLAG_SPAWNED;
    }
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    skate->ResetLevel();
    MemSys::LeaveHeapScope();
    if (script)
        script->m_spawnFlags = static_cast<uint8_t>((script->m_spawnFlags & ~Scripts::SCRIPT_FLAG_SPAWNED) | spawned);
    return true;
}

// 0x00566F40
void Skate::UpdateRecords()
{
    m_newRecord = false;
    Highscores::CStageBests* records =
        Highscores::g_gameRecords->GetLevelRecords(m_career->FetchStage());
    GameObjects::CRider* skater = Sole()->GetHomeRider();
    if (!skater)
        return;
    auto* score = static_cast<PointTally*>(skater->GetPointsTotal());
    Highscores::CBestEntryList* highScores = records->m_highScores;
    m_newRecord |= highScores->TryInsertEntry(score->GetTotalScore(), 0) != -1;
    m_newRecord |= records->m_bestCombos->TryInsertEntry(score->m_topChain, 0) != -1;
    auto* balance = reinterpret_cast<uint8_t*>(skater->FetchModule(kSkaterBalanceTrick));
    float grind = *reinterpret_cast<float*>(balance + 0xA8);   // (the longest grind, manual and lip trick: seconds)
    float manual = *reinterpret_cast<float*>(balance + 0x50);
    float lipTrick = *reinterpret_cast<float*>(balance + 0x100);
    int tricks = score->m_bestTricks;
    m_newRecord |= records->m_grindRecord->TryBeatRecord(Ftol(static_cast<double>(grind) * 100.0), 0);
    m_newRecord |= records->m_longestManual->TryBeatRecord(Ftol(static_cast<double>(manual) * 100.0), 0);
    m_newRecord |= records->m_longestLipTrick->TryBeatRecord(Ftol(static_cast<double>(lipTrick) * 100.0), 0);
    m_newRecord |= records->m_biggestChain->TryBeatRecord(tricks, 0);
}

// 0x00567580
bool Skate::AllSkatersIdle()
{
    bool idle = true;
    for (uint32_t i = 0; i < static_cast<uint32_t>(CountRiders()); ++i) {
        GameObjects::CRider* skater = SkaterAt(i); // (null past the end, as the original - never reached)
        if (GameMode()->IsTrue(kIsHorse) && static_cast<uint32_t>(m_horse->m_skater) != skater->m_ident)
            continue; // (in HORSE only the skater taking the run)
        auto* endRun = static_cast<GameObjects::CRiderSessionEnd*>(skater->FetchModule(kSkaterEndRun));
        if (endRun->RunDone())
            continue;
        idle = false;
        if (skater->m_unknownBC)
            endRun->EndRun(false);
    }
    return idle;
}

// 0x00567690
bool Skate::FirstTrickStarted()
{
    GameObjects::CRider* skater = GetRiderByNumber(static_cast<uint32_t>(m_horse->m_skater));
    auto* trick = reinterpret_cast<uint8_t*>(skater->FetchModule(kTrick));
    if (trick[0x13A8])
        static_cast<GameObjects::CRiderSessionEnd*>(skater->FetchModule(kSkaterEndRun))->EndRun(false);
    return trick[0x13A8];
}

// 0x005676E0
bool Skate::FirstTrickCompleted()
{
    GameObjects::CRider* skater = GetRiderByNumber(static_cast<uint32_t>(m_horse->m_skater));
    return reinterpret_cast<uint8_t*>(skater->FetchModule(kTrick))[0x13A9];
}

// --- the per-player settings ----------------------------------------------------------------------------------------

// 0x00567BD0
void Skate::SetTurnTapCount(int player, bool on)
{
    m_participants[player].m_spinTaps = on;
    if (GameObjects::CGameEntity* skater = GetRiderAt(player))
        reinterpret_cast<uint8_t*>(skater->FetchModule(kSkaterCorePhysics))[0x2DA] = on;
}

// 0x00567C00
void Skate::SetAutoEject(int player, bool on)
{
    m_participants[player].m_autoKick = on;
    if (GameObjects::CGameEntity* skater = GetRiderAt(player))
        reinterpret_cast<uint8_t*>(skater->FetchModule(kSkaterCorePhysics))[0x2D9] = on;
}

// 0x00567C30
void Skate::ChangeRumble(int player, int on)
{
    m_participants[player].m_vibration = static_cast<uint8_t>(on) != 0;
    if (GameObjects::CGameEntity* skater = GetRiderAt(player)) {
        if (auto* vibration = static_cast<GameObjects::CPadRumbleModule*>(skater->FetchModule(kVibration)))
            vibration->ChangeLive(static_cast<uint8_t>(on) != 0); // (the whole word passed: only its low byte read)
    }
}

// --- the module's script --------------------------------------------------------------------------------------------

// 0x00565ED0
void Skate::LaunchScript(uint32_t name, Scripts::CStruct* params)
{
    if (!m_ownScript) {
        void* memory = Scripts::CScript::operator new(sizeof(Scripts::CScript));
        m_ownScript = memory ? static_cast<Scripts::CScript*>(memory)->MakeNew() : nullptr;
    }
    m_ownScript->BindScript(name, params, nullptr);
}

// 0x00565F80
bool Skate::KillScript()
{
    Scripts::CScript* script = m_ownScript;
    if (!script)
        return false;
    script->Teardown();
    Scripts::CScript::operator delete(script);
    m_ownScript = nullptr;
    return true;
}

// 0x00565DA0 (static): the rails drawn (compiled out: the stub calls stay, with what they were given).
void __cdecl Skate::DebugRails()
{
    Skate* skate = Sole();
    if (!skate->m_debugRails)
        return;
    Stubs::IgnoreArgument(0); // (the rail manager's debug drawing)
    auto* manager = reinterpret_cast<GameObjects::CEntityHub*>(EntityHubHandle());
    for (GameObjects::CObjectModule* component = manager->FindFirstModuleOfKind(kRailManagerType); component;
         component = component->mp_nextOfType) {
        uint8_t* object = reinterpret_cast<uint8_t*>(component->m_entity);
        float matrix[16];
        std::memcpy(matrix, object + 0x6C, sizeof(matrix));
        matrix[3] = 0.0f;
        matrix[7] = 0.0f;
        matrix[11] = 0.0f;
        std::memcpy(matrix + 12, object + 0x4C, 12);
        matrix[15] = 1.0f;
        void* rails = reinterpret_cast<void*(__thiscall*)(GameObjects::CObjectModule*)>(component->m_vtable[0x34 / 4])(component);
        if (rails)
            Stubs::IgnoreArgument(static_cast<int>(reinterpret_cast<uintptr_t>(matrix)));
    }
}

} // namespace Mdl

namespace GameModules {

// --- each frame -----------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kHideLoadingScreen = 0xC5C9373A;     // hide_loading_screen
constexpr uint32_t kRestoreSkaterCamera = 0x15674315;   // restore_skater_camera
constexpr uint32_t kGameUpdate = 0x1AEF674F;            // Game_Update
constexpr uint32_t kIsChangingLevels = 0xE997DB9F;      // is_changing_levels
constexpr uint32_t kCheckForUnpluggedControllers = 0x77A5662C; // check_for_unplugged_controllers
constexpr uint32_t kRootWindow = 0x56A1EAE3;            // root_window
constexpr uint32_t kUnpluggedDialogAnchor = 0x2EDB780F; // controller_unplugged_dialog_anchor
constexpr uint32_t kUnpluggedFrameCount = 0xF7FABB50;   // controller_unplugged_frame_count
constexpr uint32_t kLeavingNetGame = 0xEAA48E14;        // leaving_net_game
constexpr uint32_t kDeviceNum = 0xC9428A08;             // device_num
constexpr uint32_t kControllerUnplugged = 0xF77ACA62;   // controller_unplugged
constexpr uint32_t kSuspend = 0xCE0CA665;               // suspend

// The game's cameras (0x004A09C0 the viewport's, 0x004A0950 the split screen's).
Gfx2D::ViewCam* ViewportCamera(int index) { return Engine3D::FetchLiveCamera(index); }
Gfx2D::ViewCam* ScreenCamera(int index) { return Engine3D::ObtainActiveLens(index); }

// The screen element with this id (its smart pointer let go at once).
MenuSystem::CUiWidget* FindElement(uint32_t id)
{
    MenuSystem::ElementPtr found;
    return MenuSystem::CUiTree::Sole()->FetchPart(&found, id, 0)->m_ownerObject;
}

} // namespace

// 0x00564D30 (an object walk's callback): the object's suspend component told to work out whether it's drawn.
void __cdecl Skate::UpdateSuspendVisibility(GameObjects::CEntity* object, void*)
{
    if (auto* suspend = static_cast<GameObjects::CDormancyModule*>(static_cast<GameObjects::CGameEntity*>(object)->FetchModule(kSuspend)))
        suspend->RefreshShownState();
}

// 0x00567710
void Skate::RunFrameLogic()
{
    ScriptCommands::g_todManager->TickFrame();
    GameObjects::NextSuspendFrame();
    if (!GameObjects::g_simulationHalted && !GameObjects::g_viewMode) {
        Stubs::DoNothing();
        if (m_movies->TotalOf()) {
            if (m_movies->ObtainActiveLens())
                Engine3D::ChooseCamera(0, static_cast<Gfx2D::ViewCam*>(m_movies->ObtainActiveLens()));
            if (m_movieCamera)
                Scripts::LaunchScript(kHideLoadingScreen, nullptr, nullptr, false, nullptr);
            reinterpret_cast<void(__thiscall*)(GameObjects::CVideoHub*)>(m_movies->m_vtable[0x20 / 4])(m_movies);
            m_movieCamera = true;
        } else if (m_movieCamera) {
            Scripts::LaunchScript(kRestoreSkaterCamera, nullptr, nullptr, false, nullptr);
            m_movieCamera = false;
        }
        if (m_objectMovies->TotalOf())
            reinterpret_cast<void(__thiscall*)(GameObjects::CVideoHub*)>(m_objectMovies->m_vtable[0x20 / 4])(m_objectMovies);
    }
    PrepareEntityUpdateData();
    Scripts::LaunchScript(kGameUpdate, nullptr, nullptr, false, nullptr);
    if (GameObjects::g_simulationHalted)
        EntityHubHandle()->ForEach(&UpdateSuspendVisibility, nullptr);
    // The proximity nodes told where the players (and their cameras) are.
    if (GetHomeRider()) {
        int screens = g_viewportCount;
        if (screens == 1) {
            if (ViewportCamera(0)) {
                const Maths::Vec4* camera = ViewportCamera(0)->FetchLocation();
                GameObjects::ProximityNotify(1, GetHomeRider(), camera, 1);
                auto* position = reinterpret_cast<Maths::Vec4*>(reinterpret_cast<uint8_t*>(GetHomeRider()) + 0x4C);
                GameObjects::ProximityNotify(1, GetHomeRider(), position, 2);
            }
        } else if (screens == 2) {
            if (ScreenCamera(0)) {
                GameObjects::CEntity* first = EntityHubHandle()->LookUpEntity(0);
                GameObjects::ProximityNotify(1, first, ScreenCamera(0)->FetchLocation(), 1);
                GameObjects::ProximityNotify(1, first, reinterpret_cast<Maths::Vec4*>(reinterpret_cast<uint8_t*>(first) + 0x4C), 2);
            }
            if (ScreenCamera(1) && EntityHubHandle()->LookUpEntity(1)) {
                GameObjects::CEntity* second = EntityHubHandle()->LookUpEntity(1);
                GameObjects::ProximityNotify(2, second, ScreenCamera(1)->FetchLocation(), 1);
                GameObjects::ProximityNotify(2, second, reinterpret_cast<Maths::Vec4*>(reinterpret_cast<uint8_t*>(second) + 0x4C), 2);
            }
        }
    } else if (static_cast<uint8_t>(ScriptCommands::CFunc_IsObserving(nullptr, nullptr)) && g_viewportCount == 1 &&
               ViewportCamera(0)) {
        GameObjects::ProximityNotify(1, nullptr, ViewportCamera(0)->FetchLocation(), 1);
    }
    if (m_stageSwitched)
        m_stageSwitched = false;
    // An unplugged controller: the dialog after controller_unplugged_frame_count frames (leaving a net game).
    int changing = Scripts::FetchIntValue(kIsChangingLevels);
    if (!Scripts::FetchIntValue(kCheckForUnpluggedControllers) || changing || !m_watchForUnplug)
        return;
    if (!FindElement(kRootWindow) || FindElement(kUnpluggedDialogAnchor))
        return;
    bool unplugged = false;
    uint8_t* device = nullptr;
    for (int i = 0; i < 8; ++i) {
        auto* skater = static_cast<GameObjects::CRider*>(GetRiderAt(i));
        if (!skater || !skater->m_unknownBC)
            continue;
        device = *reinterpret_cast<uint8_t**>(skater->m_unknown264 + 0x490);
        if (reinterpret_cast<Sio::InputPort*>(*reinterpret_cast<void**>(device + 0x1C))->IsAttached())
            continue;
        unplugged = true;
        if (m_unpluggedFrames > Scripts::FetchIntValue(kUnpluggedFrameCount)) {
            m_unpluggedFrames = 0;
            Scripts::CStruct* params = Scripts::SpawnParamBag();
            if (params)
                params = params->PrepareAll();
            OnlinePlay::Hub* net = NetSessionSlot();
            if (net->InNetGame()) {
                GoalSystem::CGoalManager* goals = GoalSystem::GetObjectiveBoard();
                goals->DisengageEveryObjective(false);
                goals->ReleaseEveryObjective();
                params->PutNameKey(0u, kLeavingNetGame);
                LeaveServer();
                if (static_cast<uint8_t>(net->IsClientSide())) {
                    net->RemovePlayers();
                    net->DisconnectClients();
                }
            }
            params->PutInt(kDeviceNum, *reinterpret_cast<int*>(*reinterpret_cast<uint8_t**>(device + 0x1C) + 0xC));
            Scripts::LaunchScript(kControllerUnplugged, params, nullptr, false, nullptr);
            DeleteStructure(params);
            ++m_unpluggedFrames;
            return;
        }
    }
    if (unplugged)
        ++m_unpluggedFrames;
    else
        m_unpluggedFrames = 0;
}

// 0x00568C90 (the logic task's code)
void __cdecl Skate::FrontEndTick(TaskList::SchedulerTask* task)
{
    Skate* skate = *reinterpret_cast<Skate**>(reinterpret_cast<uint8_t*>(task) + 0x1C);
    OnlinePlay::Hub* net = NetSessionSlot();
    skate->RunFrameLogic();
    if (net->OnServer() && skate->CountRiders())
        skate->TestRiderContacts();
    if (GameMode()->GetNameKey() != kNetKing && GameMode()->GetNameKey() != kKing)
        return;
    if (GameMode()->FinishRulesReached())
        return;
    auto* king = AsPlayer(net->GetHillRuler());
    if (!king || king->m_skater->m_isHalted)
        return;
    // The king's time counts up; the beeps as the target comes near.
    auto* score = static_cast<PointTally*>(king->m_skater->GetPointsTotal());
    int total = score->GetTotalScore();
    score->SetGrandTotal(total - Ftol(FrameClock::GetSlomoFrameSeconds() * 60.0 * static_cast<double>(-16.666666f)));
    int target = GameMode()->GetFirstConditionValue();
    int current = static_cast<uint8_t>(GameMode()->IsSquadMatch()) ? net->GetSquadPoints(king->m_team) : score->GetTotalScore();
    skate->TimerBeep(static_cast<float>(static_cast<double>(target - current) * static_cast<double>(0.001f)), 10.0f,
                     10.0f, "timer_runout_beep"); // "timer_runout_beep"
}

// --- made and destroyed ---------------------------------------------------------------------------------------------

// 0x00568DE0
Skate* Skate::MakeNew()
{
    PartUnit::MakeNew();
    m_vtable = kSkateVtable;
    m_riders.m_listLink.data = reinterpret_cast<void*>(-1);
    m_riders.m_listLink.priority = 0;
    m_riders.m_listLink.next = &m_riders.m_listLink;
    m_riders.m_listLink.prev = &m_riders.m_listLink;
    m_riders.m_listLink.vtable = kSkaterHeadVtable;
    m_watchForUnplug = false;
    m_unpluggedFrames = 0;
    auto* task = static_cast<TaskList::SchedulerTask*>(MemSys::AllocZeroed(0x20));
    if (task) {
        task->MakeNew(0);
        task->m_vtable = GameCode::kTaskVtable;
        *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(task) + 0x18) = reinterpret_cast<void*>(&FrontEndTick);
        *reinterpret_cast<Skate**>(reinterpret_cast<uint8_t*>(task) + 0x1C) = this;
    }
    m_updateJob = task;
    reinterpret_cast<SkateNet*>(this)->CreateNetworkTasks();
    GameObjects::g_profileRoster->ResetTemplates();
    void* memory = MemSys::AllocZeroed(0x10);
    m_gameFlow = memory ? static_cast<GoalSystem::CMatchFlow*>(memory)->MakeNew() : nullptr;
    memory = MemSys::AllocZeroed(0x39C);
    m_goalManager = memory ? static_cast<GoalSystem::CGoalManager*>(memory)->MakeNew() : nullptr;
    Engine3D::Engine::ChangeEntityHub(static_cast<void*>(GameObjects::g_compositeRegistry));
    memory = MemSys::AllocZeroed(0x6AC);
    m_career = memory ? static_cast<GameObjects::CRiderStoryProgress*>(memory)->MakeNew() : nullptr;
    memory = MemSys::AllocZeroed(0x54);
    m_railManager = memory ? static_cast<GameObjects::CGrindRailHub*>(memory)->PrepareAll() : nullptr;
    memory = MemSys::AllocZeroed(0x404);
    m_checksums = memory ? static_cast<GameObjects::ChecksumSet*>(memory)->MakeNew() : nullptr;
    TrickObjects()->RemoveAllClusters();
    m_levelLaunched = false;
    m_matchStyle = kFreeskate;
    memory = MemSys::AllocZeroed(0xC);
    m_splitscreenPreferences = memory ? static_cast<OptionStore::Preferences*>(memory)->PrepareAll() : nullptr;
    m_splitscreenPreferences->ReadIn(kDefaultSplitscreenPreferences);
    memory = MemSys::AllocZeroed(0x168);
    m_competition = memory ? static_cast<CContest*>(memory)->MakeNew(0) : nullptr;
    memory = MemSys::AllocZeroed(0x438);
    m_horse = memory ? static_cast<CLetterMatch*>(memory)->MakeNew() : nullptr;
    memory = MemSys::AllocZeroed(0x12C);
    m_movies = memory ? static_cast<GameObjects::CVideoHub*>(memory)->MakeNew() : nullptr;
    memory = MemSys::AllocZeroed(0x12C);
    m_objectMovies = memory ? static_cast<GameObjects::CVideoHub*>(memory)->MakeNew() : nullptr;
    Highscores::g_gameRecords->InitLevels(0x15);
    for (uint32_t i = 0; i < 8; ++i) {
        m_participants[i].m_unknown0 = static_cast<int>(i); // (its index)
        m_participants[i].m_vibration = true;
        m_participants[i].m_autoKick = true;
        if (GameObjects::CRider* skater = SkaterAt(i))
            reinterpret_cast<uint8_t*>(skater->FetchModule(kSkaterCorePhysics))[0x2D9] = 1;
        m_participants[i].m_spinTaps = false;
        if (GameObjects::CRider* skater = SkaterAt(i))
            reinterpret_cast<uint8_t*>(skater->FetchModule(kSkaterCorePhysics))[0x2DA] = 0;
    }
    m_ownScript = nullptr;
    return this;
}

// 0x005663A0
void Skate::DestroyBody()
{
    m_vtable = kSkateVtable;
    Gfx2D::CClipStore::Teardown();
    SurfaceKind::ClearTerrainTypes();
    Scripts::LaunchScript(kUninitLightManager, nullptr, nullptr, false, nullptr);
    if (CContest* competition = m_competition) {
        competition->DestroyBody();
        MemSys::Dealloc(competition);
    }
    MemSys::Dealloc(m_horse);
    if (m_movies)
        reinterpret_cast<void(__thiscall*)(GameObjects::CVideoHub*, int)>(m_movies->m_vtable[0])(m_movies, 1);
    if (m_objectMovies)
        reinterpret_cast<void(__thiscall*)(GameObjects::CVideoHub*, int)>(m_objectMovies->m_vtable[0])(m_objectMovies, 1);
    if (GameObjects::CGrindRailHub* rails = m_railManager) {
        rails->TidyUp();
        MemSys::Dealloc(rails);
    }
    if (m_career)
        reinterpret_cast<void(__thiscall*)(GameObjects::CRiderStoryProgress*, int)>(*reinterpret_cast<void***>(m_career)[0])(m_career, 1);
    reinterpret_cast<SkateNet*>(this)->DeleteNetworkTasks();
    if (m_gameFlow)
        reinterpret_cast<void(__thiscall*)(GoalSystem::CMatchFlow*, int)>(*reinterpret_cast<void***>(m_gameFlow)[0])(m_gameFlow, 1);
    if (GoalSystem::CGoalManager* goals = m_goalManager) {
        goals->DestroyBody();
        MemSys::Dealloc(goals);
    }
    if (m_updateJob)
        reinterpret_cast<void(__thiscall*)(TaskList::SchedulerTask*, int)>(m_updateJob->m_vtable[2])(m_updateJob, 1);
    if (m_splitscreenPreferences)
        reinterpret_cast<void(__thiscall*)(OptionStore::Preferences*, int)>(*reinterpret_cast<void***>(m_splitscreenPreferences)[0])(
            m_splitscreenPreferences, 1);
    MemSys::Dealloc(m_checksums);
    if (Scripts::CScript* script = m_ownScript) {
        script->Teardown();
        Scripts::CScript::operator delete(script);
    }
    SkaterHeadDestroyBody(&m_riders.m_listLink);
    PartUnit::DestroyBody();
}

// 0x00567BA0
Skate* Skate::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00565750: the skater list's node's scalar deleting destructor (ListKit::Elem<CRider>, vtable 0x0064E544).
MemSys::ListNode* __fastcall Skate::DestroySkaterNode(MemSys::ListNode* node, int, uint8_t flags)
{
    node->UnlinkAs(kSkaterNodeVtable);
    if (flags & 1)
        MemSys::Dealloc(node);
    return node;
}

// 0x00565FC0: the skater list's head's (ListKit::Foremost<CRider>, vtable 0x0064E5D4).
MemSys::ListNode* __fastcall Skate::DestroySkaterHead(MemSys::ListNode* head, int, uint8_t flags)
{
    SkaterHeadDestroyBody(head);
    if (flags & 1)
        MemSys::Dealloc(head);
    return head;
}

// 0x00569580 / 0x00569590: around the network manager's queued scripts.
void Skate::BeginQueuedScripts() { m_runningQueuedScripts = true; }
void Skate::EndQueuedScripts() { m_runningQueuedScripts = false; }

// 0x005695A0: the cheats on, as one value (their checksums OR-ed together, as the original).
uint32_t Skate::GetCheats()
{
    static const uint32_t kCheats[] = {0xEEFECF1B, 0xB38341C9, 0xCD09E062, 0x69A1CE96, 0x6B0F24BC,
                                       0x520A66EF, 0xEBA89179, 0xC4FFC672, 0xD5982173};
    uint32_t cheats = 0;
    for (uint32_t cheat : kCheats)
        if (m_career->IsCodeOn(cheat))
            cheats |= cheat;
    return cheats;
}

} // namespace Mdl
