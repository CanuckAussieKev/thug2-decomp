// The skate module's skaters (see skate_module.h): the list walked by index, added / removed / re-identified, sent to
// their restart points, cleaned up and reset with the level, the game launched, their collisions, runs and records,
// the per-player settings, the module's script, its destructor.

#include "game/skate/skate_module.h"

#include <cstring>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/stubs.h"
#include "game/core/task.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/front/front_end.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/gfx/tod_manager.h"
#include "game/sio/device.h"
#include "game/sio/inpman.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/player_info.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/obj/emitter.h"
#include "game/obj/movie_manager.h"
#include "game/obj/objman.h"
#include "game/obj/proxim.h"
#include "game/obj/suspend_component.h"
#include "game/obj/shadow_component.h"
#include "game/obj/trick_objects.h"
#include "game/obj/vibration_component.h"
#include "game/script/preferences.h"
#include "game/skate/records.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/skate/career.h"
#include "game/skate/competition.h"
#include "game/skate/game_mode.h"
#include "game/skate/horse.h"
#include "game/skate/profile_manager.h"
#include "game/skate/score.h"
#include "game/skate/skate_net.h"
#include "game/skater/end_run.h"
#include "game/obj/input_component.h"
#include "game/obj/node_components.h"
#include "game/obj/rail_manager.h"
#include "game/skater/skater.h"
#include "game/skater/state_history.h"
#include "game/skater/trick_component.h"
#include "game/sfx/terrain.h"
#include "game/sys/timer.h"
#include "game/sys/viewer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Mdl {

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

const char* Text(uint32_t address) { return reinterpret_cast<const char*>(address); }
Game::CGameMode* GameMode() { return GameVariable<Game::CGameMode*>(0x007CE470); }
GameNet::Manager* NetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
Obj::CGeneralManager* ObjectManager() { return GameVariable<Obj::CGeneralManager*>(0x006F0FB8); }
Obj::CTrickObjectManager* TrickObjects() { return GameVariable<Obj::CTrickObjectManager*>(0x007CE500); }
Script::CStruct*& CatInfo() { return GameVariable<Script::CStruct*>(0x007CE480); }
Script::CStruct*& StatGoalInfo() { return GameVariable<Script::CStruct*>(0x007CE484); }
// The C runtime's _ftol (0x006236FC): truncated toward zero, the low 32 bits of the 64-bit result.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
Mem::Heap* SkaterHeap(int index) { return Mem::Manager::Get()->m_skaterHeaps[index]; }

struct PlayerSearch {
    void** m_vtable = reinterpret_cast<void**>(0x0064C1A8);
    void* m_node = nullptr;
};
GameNet::PlayerInfo* AsPlayer(void* player) { return static_cast<GameNet::PlayerInfo*>(player); }

Script::CStruct* NewStructure()
{
    Script::CStruct* made = Script::NewStruct();
    return made ? made->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

// The skater list's node (Lst::Node<CSkater>, +0x238 of the skater) out of the list.
void UnlinkSkaterNode(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// --- the list -------------------------------------------------------------------------------------------------------

Mem::ListNode* Skate::SkaterNode(uint32_t index)
{
    return reinterpret_cast<Game::GoalListHead*>(&m_skaters)->NodeAt(static_cast<int>(index)); // (Lst::Head's GetItem)
}

// 0x005665C0
Obj::CCompositeObject* Skate::GetSkater(int index)
{
    if (static_cast<uint32_t>(GetNumSkaters()) <= static_cast<uint32_t>(index))
        return nullptr;
    return static_cast<Obj::CCompositeObject*>(SkaterNode(static_cast<uint32_t>(index))->data);
}

// The skater at `index` as the module's loops find it (null past the end; the node's data otherwise).
Obj::CSkater* Skate::SkaterAt(uint32_t index)
{
    if (static_cast<uint32_t>(GetNumSkaters()) <= index)
        return nullptr;
    return static_cast<Obj::CSkater*>(SkaterNode(index)->data);
}

// 0x00566520
Obj::CSkater* Skate::GetLocalSkater()
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(GetNumSkaters()); ++i) {
        auto* skater = static_cast<Obj::CSkater*>(SkaterNode(i)->data); // (no check: as the original)
        if (skater->m_unknownBC)
            return skater;
    }
    return nullptr;
}

// 0x00566600
Obj::CSkater* Skate::GetSkaterById(uint32_t id)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(GetNumSkaters()); ++i) {
        auto* skater = static_cast<Obj::CSkater*>(SkaterNode(i)->data);
        if (skater->m_id == id)
            return skater;
    }
    return nullptr;
}

// The two lookups keep ECX (the original's leave it alone, and the whole-program-optimised callers rely on it: DoUpdate
// calls GetLocalSkater twice with the module in ECX).
namespace {
Obj::CSkater* __fastcall LocalSkaterOf(Skate* skate) { return skate->GetLocalSkater(); }
Obj::CSkater* __fastcall SkaterByIdOf(Skate* skate, int, uint32_t id) { return skate->GetSkaterById(id); }
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
void Skate::RemoveSkater(Obj::CSkater* skater)
{
    if (Obj::CObject* board = ObjectManager()->GetObjectByID(skater->m_id + kBailBoard)) {
        reinterpret_cast<void(__thiscall*)(Obj::CObject*)>(board->m_vtable[0x18 / 4])(board); // (marked dead)
        ObjectManager()->FlushDeadObjects();
    }
    if (!skater->m_unknownBC) {
        Script::CStruct* params = NewStructure();
        params->AddChecksum(kSkaterId, skater->m_id);
        Script::RunScript(kRemoveClientSkater, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    if (skater->m_unknownBC && skater->m_viewport == 0) {
        // The local player's created tricks and stat goals kept for the skater that replaces it.
        if (CatInfo()) {
            DeleteStructure(CatInfo());
            CatInfo() = nullptr;
        }
        Dbg::Printf(Text(0x0064E568)); // "CAT: Storing Created Trick data\n"
        CatInfo() = NewStructure();
        skater->WriteCATInfo(CatInfo());
        if (StatGoalInfo()) {
            DeleteStructure(StatGoalInfo());
            StatGoalInfo() = nullptr;
        }
        Dbg::Printf(Text(0x0064E548)); // "CAT: Storing Stat Goal data\n"
        StatGoalInfo() = NewStructure();
        skater->WriteList44B66A9F(StatGoalInfo());
    }
    UnlinkSkaterNode(static_cast<Mem::ListNode*>(skater->GetBlock238()));
    reinterpret_cast<void(__thiscall*)(Obj::CSkater*, int)>(skater->m_vtable[0])(skater, 1);
}

// 0x00567180
Obj::CSkater* Skate::AddSkater(Obj::CSkaterProfile* profile, bool local, uint32_t id, int viewport)
{
    GameNet::Manager* net = NetManager();
    if (local) {
        Obj::CSkaterProfile* source = GameVariable<Obj::CPlayerProfileManager*>(0x007CE4B4)->GetProfile(
            static_cast<uint8_t>(CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr)) ? viewport : 0);
        GameFunction<void(__thiscall*)(Obj::CSkaterProfile*, Obj::CSkaterProfile*)>(0x004893A0)(profile, source);
        Script::RunScript(kShowPanelStuff, nullptr, nullptr, false, nullptr);
        Obj::CSkater* skater = GetLocalSkater();
        if (skater) {
            if (static_cast<uint8_t>(CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr))) {
                if (skater->m_id == id)
                    return skater;
            } else {
                if (skater->m_id == id)
                    return skater;
                // The local skater given the new id (its place in the list by it, its particles, its score's).
                auto* node = static_cast<Mem::ListNode*>(skater->GetBlock238());
                node->priority = static_cast<int>(id);
                UnlinkSkaterNode(node);
                reinterpret_cast<Lst::Head*>(&m_skaters)->AddUniqueSequence(node);
                Mem::PushContext(SkaterHeap(skater->GetSkaterNumber()));
                Script::RunScript(kDestroySkaterParticles, nullptr, skater, false, nullptr);
                skater->SetID(id);
                Script::RunScript(kInitSkaterParticles, nullptr, skater, false, nullptr);
                Mem::PopContext();
                static_cast<Score*>(skater->GetScore())->SetSkaterId(static_cast<int16_t>(id));
                return skater;
            }
        } else if (Obj::CObject* camera = ObjectManager()->GetObjectByID(kSkaterCam0)) {
            camera->m_objectFlags &= ~2u;
            reinterpret_cast<void(__thiscall*)(Obj::CObject*, int)>(camera->m_vtable[0])(camera, 1);
        }
    } else if (net->InNetGame()) {
        if (Obj::CSkater* skater = GetLocalSkater()) {
            if (auto* vibration = static_cast<Obj::CVibrationComponent*>(skater->GetComponent(kVibration)))
                vibration->StopAll();
        }
    }
    net->InNetGame(); // (its answer unused, as the original)
    Mem::PushMemProfile(Text(0x0064E724)); // "Skater"
    int heap = GetNextUnusedSkaterHeapIndex(local);
    Mem::PushContext(SkaterHeap(heap));
    void* memory = Mem::Calloc(sizeof(Obj::CSkater));
    Obj::CSkater* skater = memory ? static_cast<Obj::CSkater*>(memory)->Construct(
                                        viewport, reinterpret_cast<Obj::CCompositeObjectManager*>(ObjectManager()),
                                        local, id, heap, 1)
                                  : nullptr;
    reinterpret_cast<void(__thiscall*)(Obj::CSkater*, Obj::CSkaterProfile*)>(skater->m_vtable[0x34 / 4])(skater, profile);
    if (static_cast<uint8_t>(CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr))) {
        Script::CStruct* params = NewStructure();
        params->AddInteger(kPlayer, 0);
        Script::RunScript(kModelApplyCustomLogos, params, nullptr, false, nullptr);
        DeleteStructure(params);
    }
    if (!skater->m_unknownBC)
        Script::RunScript(kClientSkaterInit, nullptr, skater, false, nullptr);
    auto* node = static_cast<Mem::ListNode*>(skater->GetBlock238());
    node->priority = static_cast<int>(skater->m_id);
    reinterpret_cast<Lst::Head*>(&m_skaters)->AddUniqueSequence(node);
    BindControllers();
    if (skater->m_unknownBC && skater->m_viewport == 0) {
        if (CatInfo()) {
            Dbg::Printf(Text(0x0064E704)); // "CAT: Restoring Created Tricks\n"
            skater->LoadCATInfo(CatInfo());
            DeleteStructure(CatInfo());
            CatInfo() = nullptr;
        }
        if (StatGoalInfo()) {
            Dbg::Printf(Text(0x0064E6E4)); // "CAT: Restoring Stat Goal data\n"
            skater->LoadList44B66A9F(StatGoalInfo());
            DeleteStructure(StatGoalInfo());
            StatGoalInfo() = nullptr;
        }
    }
    if (skater->m_id == 1 && static_cast<uint8_t>(CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr)))
        static_cast<Obj::CTrickComponent*>(skater->GetComponent(kTrick))->SetTrickMappings(profile);
    Mem::PopMemProfile();
    Mem::PopContext();
    return skater;
}

// 0x00565FF0
int Skate::GetNextUnusedSkaterHeapIndex(bool local)
{
    int index = local ? 0 : 1;
    if (GameMode()->GetMaximumNumberOfPlayers() <= 0)
        return index;
    for (int tries = 0;;) {
        bool used = false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(GetNumSkaters()); ++i) {
            if (static_cast<Obj::CSkater*>(SkaterNode(i)->data)->GetSkaterNumber() == index) {
                used = true;
                break;
            }
        }
        if (!used)
            return index;
        index = static_cast<int>(static_cast<uint32_t>(index + 1) %
                                 static_cast<uint32_t>(GameMode()->GetMaximumNumberOfPlayers()));
        if (++tries >= GameMode()->GetMaximumNumberOfPlayers())
            return index;
    }
}

// 0x00565BF0
void Skate::SetSkaterShadow(Obj::CSkater* skater)
{
    auto* shadow = static_cast<Obj::CShadowComponent*>(skater->GetComponent(kShadow));
    int type = GameVariable<uint8_t>(0x007D6435) ? 1 : 2;
    Mem::PushContext(SkaterHeap(skater->GetSkaterNumber()));
    if (static_cast<uint8_t>(CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr)))
        type = 2;
    if (!skater->m_unknownBC)
        type = 2;
    Script::CStruct* params = NewStructure();
    params->AddFloat(kShadowScale, 1.0f);
    params->AddString(kShadowModel, Text(0x006464CC)); // "Ped_Shadow"
    reinterpret_cast<void(__thiscall*)(Obj::CShadowComponent*, Script::CStruct*)>(shadow->m_vtable[2])(shadow, params);
    DeleteStructure(params);
    shadow->DestroyShadow();
    shadow->CreateShadow(type, Text(0x006464CC));
    Mem::PopContext();
}

// 0x00567090
void Skate::BindControllers()
{
    for (uint32_t i = 0; i < 8; ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        if (!skater || !skater->m_unknownBC)
            continue;
        int number = skater->GetSkaterNumber();
        auto* input = GameVariable<Inp::Manager*>(0x00696D40);
        int controller = input->GetPlayerController(number);
        static_cast<Obj::CInputComponent*>(skater->GetComponent(kInput))->BindToController(controller);
        if (auto* camera = static_cast<Obj::CCompositeObject*>(skater->GetSkaterCamera())) {
            controller = GameVariable<Inp::Manager*>(0x00696D40)->GetPlayerController(number);
            static_cast<Obj::CInputComponent*>(camera->GetComponent(kInput))->BindToController(controller);
        }
    }
}

// --- the restart points ---------------------------------------------------------------------------------------------

// 0x00566100
uint32_t Skate::SkipToRestartPoint(uint32_t skaterId)
{
    GameNet::Manager* net = NetManager();
    auto generic = [] {
        Dbg::Printf(Text(0x0064E600)); // "WARNING - no Player1 restart point, trying Generic"
        uint32_t node = Obj::GetRestartNode(0, 0, 0);
        if (!node)
            Dbg::Printf(Text(0x0064E5D8)); // "WARNING - No restart points found"
        return node;
    };
    auto found = [&](uint32_t node) { return node ? node : generic(); };
    auto multiplayer = [&](uint32_t start) {
        uint32_t node = Obj::GetRestartNode(kMultiplayer, static_cast<int>(net->GetStartInfoWord(static_cast<int>(start))), 0);
        if (node)
            return node;
        Dbg::Printf(Text(0x0064E638)); // "WARNING - no multiplayer restart point, trying player1"
        return found(Obj::GetRestartNode(kPlayer1, 0, 0));
    };
    if (!net->InNetGame() && GameMode()->GetInitialNumberOfPlayers() <= 1)
        return found(Obj::GetRestartNode(kPlayer1, static_cast<int>(skaterId), 0));
    if (static_cast<uint8_t>(GameMode()->IsTeamGame())) {
        if (GameMode()->GetNameChecksum() == kNetLobby)
            return found(Obj::GetRestartNode(kTeam, 0, 0));
        if (GameMode()->GetNameChecksum() == kNetCtf) {
            auto* local = AsPlayer(net->GetLocalPlayer());
            if (!local || static_cast<uint32_t>(local->m_team) > 3)
                return generic();
            return kTeamRestarts[local->m_team];
        }
        if (GameMode()->GetNameChecksum() == kNetScavengerHunt) {
            auto* local = AsPlayer(net->GetLocalPlayer());
            if (!local)
                return generic();
            Game::CGoal* hunt = Game::GetGoalManager()->GetGoal(kScavenger, 1);
            int team = Game::Virtual<bool>(hunt, Game::CGoal::kVtIsActive) ? local->m_team
                                                                             : (local->m_team + 1) % GameMode()->NumTeams();
            if (static_cast<uint32_t>(team) > 3)
                return generic();
            return kTeamRestarts[team];
        }
        return multiplayer(skaterId);
    }
    if (GameMode()->IsTrue(kIsHorse))
        return found(Obj::GetRestartNode(kHorse, m_horse->GetRestart(), 0));
    uint32_t start = skaterId;
    if (GameMode()->GetNameChecksum() == kNetScavengerHunt || GameMode()->GetNameChecksum() == kScavengerHunt) {
        Game::CGoal* hunt = Game::GetGoalManager()->GetGoal(kScavenger, 1);
        if (!Game::Virtual<bool>(hunt, Game::CGoal::kVtIsActive))
            start = (skaterId + 1) % static_cast<uint32_t>(GetNumSkaters());
    }
    return multiplayer(start);
}

// 0x00566370
void Skate::MoveToRestart(Obj::CSkater* skater, uint32_t node, uint32_t walking)
{
    if (!node) {
        node = SkipToRestartPoint(skater->m_id);
        if (!node)
            return;
    }
    reinterpret_cast<void(__thiscall*)(Obj::CSkater*, uint32_t, uint32_t)>(skater->m_vtable[0x3C / 4])(skater, node,
                                                                                                          walking);
}

// 0x00566980
void Skate::ResetSkaters(uint32_t node, uint32_t walking)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(GetNumSkaters()); ++i) {
        Obj::CSkater* skater = SkaterAt(i); // (no check: as the original)
        if (skater->m_unknownBC) {
            uint32_t at = node ? node : SkipToRestartPoint(skater->m_id);
            if (at)
                reinterpret_cast<void(__thiscall*)(Obj::CSkater*, uint32_t, uint32_t)>(skater->m_vtable[0x3C / 4])(
                    skater, at, walking);
        }
        skater->Reset27C();
    }
    m_endRun = false;
}

} // namespace Mdl

namespace Mdl {

// --- the level's session --------------------------------------------------------------------------------------------

// 0x005666A0
void Skate::Cleanup()
{
    Dbg::Printf(Text(0x0064E680)); // "Skate::Cleanup() - Deleting all session specific objects\n"
    for (uint32_t i = 0; i < static_cast<uint32_t>(GetNumSkaters()); ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        if (!skater)
            continue;
        skater->m_objectFlags |= 2; // (kept through the flush below)
        if (skater->m_unknownBC) {
            skater->Restart(0, 1);
            m_movies->ClearMovies();
        }
        skater->ResetComponents();
        skater->HideSkater();
    }
    m_movies->ClearMovies();
    m_objectMovies->StopAll();
    m_objectMovies->ClearMovies();
    if (Viewer* viewer = GetViewer())
        viewer->RemoveViewerObject();
    ObjectManager()->ForEach(&Obj::DestroyIfUnlocked, nullptr);
    ObjectManager()->FlushDeadObjects();
    for (uint32_t i = 0; i < static_cast<uint32_t>(GetNumSkaters()); ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        if (!skater)
            continue;
        skater->m_objectFlags &= ~2u;
        skater->ShowSkater();
    }
    NetManager()->ClearCrown();
    Script::DeleteSpawnedScripts();
    TrickObjects()->RemoveAllClusters();
    GameFunction<void(__thiscall*)(void*)>(0x0049AE30)(GameVariable<void*>(0x006FA7C0));
    m_railManager->Cleanup();
    Obj::Proxim_Cleanup();
    Obj::Emitter_Cleanup();
    GameFunction<void(__thiscall*)(void*)>(0x00496700)(GameVariable<void*>(0x006FA7C4));
}

// 0x00565A00
void Skate::ChangeLevel(uint32_t level)
{
    Dbg::Printf(Text(0x0064E590), level); // "-----...Skate::ChangeLevel(%d)\n"
    Mem::PushContext(Mem::Manager::Get()->m_skaterInfoHeap);
    Script::CStruct* params = NewStructure();
    params->Clear();
    params->AddComponentOfType(kLevel, Script::ESYMBOLTYPE_NAME, level);
    Script::RunScript(kRequestLevel, params, nullptr, false, nullptr);
    DeleteStructure(params);
    m_previousLevel = m_level;
    m_level = level;
    params = NewStructure();
    params->Clear();
    if (level != kLoadMainMenu)
        params->AddComponentOfType(0, Script::ESYMBOLTYPE_NAME, kPreserveSkaters);
    CFuncs::CFunc_Cleanup(params, nullptr);
    DeleteStructure(params);
    m_levelChanged = true;
    m_gameFlow->Request(kChangeLevelGameFlow);
    Mem::PopContext();
}

// 0x005668A0
void Skate::RestartLevel()
{
    m_endRun = false;
    if ((GetNumSkaters() || NetManager()->InNetGame()) && m_levelLaunched) {
        m_career->StartLevel(-1);
        Stubs::DoNothing(); // (a compiled-out call on the trick object manager)
    }
}

// 0x00566900
void Skate::ResetLevel()
{
    GameNet::Manager* net = NetManager();
    Cleanup();
    CFuncs::CFunc_ParseNodeArray(nullptr, nullptr);
    m_endRun = false;
    TrickObjects()->ApplyPlayerGraffiti();
    Game::CGoalManager* goals = Game::GetGoalManager();
    if (net->InNetGame())
        goals->UnBeatAllGoals();
    else
        goals->InitializeAllGoals();
    goals->CreateGoalLevelObjects();
    GameVariable<Gfx::CTODManager*>(0x007CE490)->ClearDeadObjects();
    Script::RunScript(kResetLevel, nullptr, nullptr, false, nullptr);
}

// 0x00566A60
void Skate::LaunchGame()
{
    GameNet::Manager* net = NetManager();
    GameVariable<FrontEnd*>(0x007CE464)->PauseGame(false);
    Mem::PushContext(Mem::BottomUpHeap());
    Dbg::Printf(Text(0x0064E6BC), GetNumSkaters()); // "\nLAUNCH GAME CALLED with %d skaters \n\n"
    if (!net->GetLocalPlayer()) {
        if (static_cast<uint32_t>(GameMode()->GetMaximumNumberOfPlayers()) > 1)
            Mem::Manager::Get()->InitExtraSkaterHeaps(GameMode()->GetMaximumNumberOfPlayers());
        m_gameFlow->Request(kInitializeGameFlow);
        m_gameFlow->Update();
    } else {
        ResetLevel();
        m_gameFlow->Request(kStandardGameFlow);
    }
    PlayerSearch search;
    if (auto* king = AsPlayer(net->GetKingOfTheHill()))
        king->MarkAsKing(false);
    for (void* player = net->FirstPlayerInfo(&search, true); player; player = net->NextPlayerInfo(&search, true))
        AsPlayer(player)->ClearFlags();
    if (GameMode()->GetNameChecksum() == kNetLobby)
        net->CreateTeamFlags(GameMode()->NumTeams());
    Mem::PopContext();
}

// 0x00566BE0
void Skate::CheckSkaterCollisions()
{
    GameNet::Manager* net = NetManager();
    if (net->IsGameOver())
        return;
    if (!net->PlayerCollisionEnabled() && GameMode()->GetNameChecksum() != kNetFirefight &&
        GameMode()->GetNameChecksum() != kFirefight && GameMode()->GetNameChecksum() != kNetKing &&
        GameMode()->GetNameChecksum() != kKing && GameMode()->GetNameChecksum() != kNetCtf)
        return;
    for (uint32_t i = 0; i < 8; ++i) {
        if (Obj::CSkater* skater = SkaterAt(i))
            reinterpret_cast<Obj::CSkaterStateHistoryComponent*>(skater->m_unknown27C)->CheckForCollisions(0);
    }
    if (GameMode()->GetNameChecksum() != kNetKing && GameMode()->GetNameChecksum() != kKing)
        return;
    auto* crown = static_cast<Obj::CCrownObject*>(net->GetCrown());
    if (!crown || crown->IsWorn())
        return;
    // The crown to the first skater near the local one.
    Obj::CSkater* king = nullptr;
    for (uint32_t i = 0; i < 8 && !king; ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        if (skater && reinterpret_cast<Obj::CSkaterStateHistoryComponent*>(skater->m_unknown27C)->IsNearLocalSkater())
            king = skater;
    }
    if (!king)
        return;
    PlayerSearch search;
    AsPlayer(net->PlayerByObjectId(static_cast<uint16_t>(king->m_id)))->MarkAsKing(true);
    for (void* player = net->FirstPlayerInfo(&search, true); player; player = net->NextPlayerInfo(&search, true)) {
        if (static_cast<uint8_t>(AsPlayer(player)->IsLocalPlayer()))
            continue;
        Net::MsgDesc desc;
        desc.Construct();
        Net::App* server = net->m_server; // (0x005480C0)
        uint8_t id = static_cast<uint8_t>(king->m_id);
        desc.m_data = &id;
        desc.m_length = 1;
        desc.m_id = kMsgKing;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        server->EnqueueMessage(AsPlayer(player)->GetConnHandle(), &desc);
    }
}

// 0x00566E70
bool __cdecl Skate::CFunc_Retry(Script::CStruct*, Script::CScript*)
{
    Mem::PushContext(Mem::BottomUpHeap());
    Instance()->LaunchGame();
    Mem::PopContext();
    return true;
}

// 0x00566EA0
bool __cdecl Skate::CFunc_LaunchGame(Script::CStruct*, Script::CScript*)
{
    Mem::PushContext(Mem::BottomUpHeap());
    Instance()->LaunchGame();
    Mem::PopContext();
    return true;
}

// 0x00566ED0
bool __cdecl Skate::CFunc_ResetLevel(Script::CStruct*, Script::CScript* script)
{
    // (the calling script isn't spawned while the level resets: its flag put back after)
    uint8_t spawned = 0;
    Skate* skate = Instance();
    if (script) {
        spawned = script->m_spawnFlags & Script::SCRIPT_FLAG_SPAWNED;
        script->m_spawnFlags &= ~Script::SCRIPT_FLAG_SPAWNED;
    }
    Mem::PushContext(Mem::BottomUpHeap());
    skate->ResetLevel();
    Mem::PopContext();
    if (script)
        script->m_spawnFlags = static_cast<uint8_t>((script->m_spawnFlags & ~Script::SCRIPT_FLAG_SPAWNED) | spawned);
    return true;
}

// 0x00566F40
void Skate::UpdateRecords()
{
    m_newRecord = false;
    Records::CLevelRecords* records =
        GameVariable<Records::CGameRecords*>(0x007CE4BC)->GetLevelRecords(m_career->GetLevel());
    Obj::CSkater* skater = Instance()->GetLocalSkater();
    if (!skater)
        return;
    auto* score = static_cast<Score*>(skater->GetScore());
    Records::CRecordTable* highScores = records->m_highScores;
    m_newRecord |= highScores->MaybeNewEntry(score->GetTotalScore(), 0) != -1;
    m_newRecord |= records->m_bestCombos->MaybeNewEntry(score->m_bestCombo, 0) != -1;
    auto* balance = reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterBalanceTrick));
    float grind = *reinterpret_cast<float*>(balance + 0xA8);   // (the longest grind, manual and lip trick: seconds)
    float manual = *reinterpret_cast<float*>(balance + 0x50);
    float lipTrick = *reinterpret_cast<float*>(balance + 0x100);
    int tricks = score->m_bestTricks;
    m_newRecord |= records->m_longestGrind->MaybeNewRecord(Ftol(static_cast<double>(grind) * 100.0), 0);
    m_newRecord |= records->m_longestManual->MaybeNewRecord(Ftol(static_cast<double>(manual) * 100.0), 0);
    m_newRecord |= records->m_longestLipTrick->MaybeNewRecord(Ftol(static_cast<double>(lipTrick) * 100.0), 0);
    m_newRecord |= records->m_longestCombo->MaybeNewRecord(tricks, 0);
}

// 0x00567580
bool Skate::AllSkatersIdle()
{
    bool idle = true;
    for (uint32_t i = 0; i < static_cast<uint32_t>(GetNumSkaters()); ++i) {
        Obj::CSkater* skater = SkaterAt(i); // (null past the end, as the original - never reached)
        if (GameMode()->IsTrue(kIsHorse) && static_cast<uint32_t>(m_horse->m_skater) != skater->m_id)
            continue; // (in HORSE only the skater taking the run)
        auto* endRun = static_cast<Obj::CSkaterEndRunComponent*>(skater->GetComponent(kSkaterEndRun));
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
    Obj::CSkater* skater = GetSkaterById(static_cast<uint32_t>(m_horse->m_skater));
    auto* trick = reinterpret_cast<uint8_t*>(skater->GetComponent(kTrick));
    if (trick[0x13A8])
        static_cast<Obj::CSkaterEndRunComponent*>(skater->GetComponent(kSkaterEndRun))->EndRun(false);
    return trick[0x13A8];
}

// 0x005676E0
bool Skate::FirstTrickCompleted()
{
    Obj::CSkater* skater = GetSkaterById(static_cast<uint32_t>(m_horse->m_skater));
    return reinterpret_cast<uint8_t*>(skater->GetComponent(kTrick))[0x13A9];
}

// --- the per-player settings ----------------------------------------------------------------------------------------

// 0x00567BD0
void Skate::SetSpinTaps(int player, bool on)
{
    m_players[player].m_spinTaps = on;
    if (Obj::CCompositeObject* skater = GetSkater(player))
        reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterCorePhysics))[0x2DA] = on;
}

// 0x00567C00
void Skate::SetAutoKick(int player, bool on)
{
    m_players[player].m_autoKick = on;
    if (Obj::CCompositeObject* skater = GetSkater(player))
        reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterCorePhysics))[0x2D9] = on;
}

// 0x00567C30
void Skate::SetVibration(int player, int on)
{
    m_players[player].m_vibration = static_cast<uint8_t>(on) != 0;
    if (Obj::CCompositeObject* skater = GetSkater(player)) {
        if (auto* vibration = static_cast<Obj::CVibrationComponent*>(skater->GetComponent(kVibration)))
            vibration->SetActive(static_cast<uint8_t>(on) != 0); // (the whole word passed: only its low byte read)
    }
}

// --- the module's script --------------------------------------------------------------------------------------------

// 0x00565ED0
void Skate::RunScript(uint32_t name, Script::CStruct* params)
{
    if (!m_script) {
        void* memory = Script::CScript::operator new(sizeof(Script::CScript));
        m_script = memory ? static_cast<Script::CScript*>(memory)->Construct() : nullptr;
    }
    m_script->SetScript(name, params, nullptr);
}

// 0x00565F80
bool Skate::KillScript()
{
    Script::CScript* script = m_script;
    if (!script)
        return false;
    script->Destroy();
    Script::CScript::operator delete(script);
    m_script = nullptr;
    return true;
}

// 0x00565DA0 (static): the rails drawn (compiled out: the stub calls stay, with what they were given).
void __cdecl Skate::DebugRails()
{
    Skate* skate = Instance();
    if (!skate->m_debugRails)
        return;
    Stubs::IgnoreArgument(0); // (the rail manager's debug drawing)
    auto* manager = reinterpret_cast<Obj::CCompositeObjectManager*>(ObjectManager());
    for (Obj::CBaseComponent* component = manager->GetFirstComponentByType(kRailManagerType); component;
         component = component->mp_nextOfType) {
        uint8_t* object = reinterpret_cast<uint8_t*>(component->m_object);
        float matrix[16];
        std::memcpy(matrix, object + 0x6C, sizeof(matrix));
        matrix[3] = 0.0f;
        matrix[7] = 0.0f;
        matrix[11] = 0.0f;
        std::memcpy(matrix + 12, object + 0x4C, 12);
        matrix[15] = 1.0f;
        void* rails = reinterpret_cast<void*(__thiscall*)(Obj::CBaseComponent*)>(component->m_vtable[0x34 / 4])(component);
        if (rails)
            Stubs::IgnoreArgument(static_cast<int>(reinterpret_cast<uintptr_t>(matrix)));
    }
}

} // namespace Mdl

namespace Mdl {

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
Gfx::Camera* ViewportCamera(int index) { return GameFunction<Gfx::Camera*(__cdecl*)(int)>(0x004A09C0)(index); }
Gfx::Camera* ScreenCamera(int index) { return GameFunction<Gfx::Camera*(__cdecl*)(int)>(0x004A0950)(index); }

// The screen element with this id (its smart pointer let go at once).
Front::CScreenElement* FindElement(uint32_t id)
{
    Front::ElementPtr found;
    return Front::CScreenElementManager::Instance()->GetElement(&found, id, 0)->mp_object;
}

} // namespace

// 0x00564D30 (an object walk's callback): the object's suspend component told to work out whether it's drawn.
void __cdecl Skate::UpdateSuspendVisibility(Obj::CObject* object, void*)
{
    if (auto* suspend = static_cast<Obj::CSuspendComponent*>(static_cast<Obj::CCompositeObject*>(object)->GetComponent(kSuspend)))
        suspend->UpdateVisibility();
}

// 0x00567710
void Skate::DoUpdate()
{
    GameVariable<Gfx::CTODManager*>(0x007CE490)->Update();
    Obj::NextSuspendFrame();
    if (!GameVariable<bool>(0x006FA7D4) && !GameVariable<int>(0x007CE4A4)) {
        Stubs::DoNothing();
        if (m_movies->Count()) {
            if (m_movies->GetCamera())
                GameFunction<void(__cdecl*)(int, void*)>(0x004A0970)(0, m_movies->GetCamera());
            if (m_movieCamera)
                Script::RunScript(kHideLoadingScreen, nullptr, nullptr, false, nullptr);
            reinterpret_cast<void(__thiscall*)(Obj::CMovieManager*)>(m_movies->m_vtable[0x20 / 4])(m_movies);
            m_movieCamera = true;
        } else if (m_movieCamera) {
            Script::RunScript(kRestoreSkaterCamera, nullptr, nullptr, false, nullptr);
            m_movieCamera = false;
        }
        if (m_objectMovies->Count())
            reinterpret_cast<void(__thiscall*)(Obj::CMovieManager*)>(m_objectMovies->m_vtable[0x20 / 4])(m_objectMovies);
    }
    PreCalculateObjectUpdateInfo();
    Script::RunScript(kGameUpdate, nullptr, nullptr, false, nullptr);
    if (GameVariable<bool>(0x006FA7D4))
        ObjectManager()->ForEach(&UpdateSuspendVisibility, nullptr);
    // The proximity nodes told where the players (and their cameras) are.
    if (GetLocalSkater()) {
        int screens = GameVariable<int>(0x00701378);
        if (screens == 1) {
            if (ViewportCamera(0)) {
                const Mth::Vector* camera = ViewportCamera(0)->GetPos();
                Obj::Proxim_Update(1, GetLocalSkater(), camera, 1);
                auto* position = reinterpret_cast<Mth::Vector*>(reinterpret_cast<uint8_t*>(GetLocalSkater()) + 0x4C);
                Obj::Proxim_Update(1, GetLocalSkater(), position, 2);
            }
        } else if (screens == 2) {
            if (ScreenCamera(0)) {
                Obj::CObject* first = ObjectManager()->GetObjectByID(0);
                Obj::Proxim_Update(1, first, ScreenCamera(0)->GetPos(), 1);
                Obj::Proxim_Update(1, first, reinterpret_cast<Mth::Vector*>(reinterpret_cast<uint8_t*>(first) + 0x4C), 2);
            }
            if (ScreenCamera(1) && ObjectManager()->GetObjectByID(1)) {
                Obj::CObject* second = ObjectManager()->GetObjectByID(1);
                Obj::Proxim_Update(2, second, ScreenCamera(1)->GetPos(), 1);
                Obj::Proxim_Update(2, second, reinterpret_cast<Mth::Vector*>(reinterpret_cast<uint8_t*>(second) + 0x4C), 2);
            }
        }
    } else if (static_cast<uint8_t>(CFuncs::CFunc_IsObserving(nullptr, nullptr)) && GameVariable<int>(0x00701378) == 1 &&
               ViewportCamera(0)) {
        Obj::Proxim_Update(1, nullptr, ViewportCamera(0)->GetPos(), 1);
    }
    if (m_levelChanged)
        m_levelChanged = false;
    // An unplugged controller: the dialog after controller_unplugged_frame_count frames (leaving a net game).
    int changing = Script::GetInteger(kIsChangingLevels);
    if (!Script::GetInteger(kCheckForUnpluggedControllers) || changing || !m_checkControllers)
        return;
    if (!FindElement(kRootWindow) || FindElement(kUnpluggedDialogAnchor))
        return;
    bool unplugged = false;
    uint8_t* device = nullptr;
    for (int i = 0; i < 8; ++i) {
        auto* skater = static_cast<Obj::CSkater*>(GetSkater(i));
        if (!skater || !skater->m_unknownBC)
            continue;
        device = *reinterpret_cast<uint8_t**>(skater->m_unknown264 + 0x490);
        if (reinterpret_cast<Sio::Device*>(*reinterpret_cast<void**>(device + 0x1C))->IsPluggedIn())
            continue;
        unplugged = true;
        if (m_unpluggedFrames > Script::GetInteger(kUnpluggedFrameCount)) {
            m_unpluggedFrames = 0;
            Script::CStruct* params = Script::NewStruct();
            if (params)
                params = params->Init();
            GameNet::Manager* net = NetManager();
            if (net->InNetGame()) {
                Game::CGoalManager* goals = Game::GetGoalManager();
                goals->DeactivateAllGoals(false);
                goals->UninitializeAllGoals();
                params->AddChecksum(0u, kLeavingNetGame);
                LeaveServer();
                if (static_cast<uint8_t>(net->OnClient())) {
                    net->RemovePlayers();
                    net->DisconnectClients();
                }
            }
            params->AddInteger(kDeviceNum, *reinterpret_cast<int*>(*reinterpret_cast<uint8_t**>(device + 0x1C) + 0xC));
            Script::RunScript(kControllerUnplugged, params, nullptr, false, nullptr);
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
void __cdecl Skate::LogicTask(Tsk::BaseTask* task)
{
    Skate* skate = *reinterpret_cast<Skate**>(reinterpret_cast<uint8_t*>(task) + 0x1C);
    GameNet::Manager* net = NetManager();
    skate->DoUpdate();
    if (net->OnServer() && skate->GetNumSkaters())
        skate->CheckSkaterCollisions();
    if (GameMode()->GetNameChecksum() != kNetKing && GameMode()->GetNameChecksum() != kKing)
        return;
    if (GameMode()->EndConditionsMet())
        return;
    auto* king = AsPlayer(net->GetKingOfTheHill());
    if (!king || king->m_skater->m_paused)
        return;
    // The king's time counts up; the beeps as the target comes near.
    auto* score = static_cast<Score*>(king->m_skater->GetScore());
    int total = score->GetTotalScore();
    score->SetTotalScore(total - Ftol(Tmr::GetSlomoFrameSeconds() * 60.0 * static_cast<double>(-16.666666f)));
    int target = GameMode()->GetFirstConditionValue();
    int current = static_cast<uint8_t>(GameMode()->IsTeamGame()) ? net->GetTeamScore(king->m_team) : score->GetTotalScore();
    skate->TimerBeep(static_cast<float>(static_cast<double>(target - current) * static_cast<double>(0.001f)), 10.0f,
                     10.0f, Text(0x0064E774)); // "timer_runout_beep"
}

// --- made and destroyed ---------------------------------------------------------------------------------------------

// 0x00568DE0
Skate* Skate::Construct()
{
    Module::Construct();
    m_vtable = reinterpret_cast<void**>(0x0064E674);
    m_skaters.m_node.data = reinterpret_cast<void*>(-1);
    m_skaters.m_node.priority = 0;
    m_skaters.m_node.next = &m_skaters.m_node;
    m_skaters.m_node.prev = &m_skaters.m_node;
    m_skaters.m_node.vtable = reinterpret_cast<void**>(0x0064E5D4);
    m_checkControllers = false;
    m_unpluggedFrames = 0;
    auto* task = static_cast<Tsk::BaseTask*>(Mem::Calloc(0x20));
    if (task) {
        task->Construct(0);
        task->m_vtable = reinterpret_cast<void**>(0x006476F0);
        *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(task) + 0x18) = reinterpret_cast<void*>(&LogicTask);
        *reinterpret_cast<Skate**>(reinterpret_cast<uint8_t*>(task) + 0x1C) = this;
    }
    m_logicTask = task;
    reinterpret_cast<SkateNet*>(this)->CreateNetworkTasks();
    GameVariable<Obj::CPlayerProfileManager*>(0x007CE4B4)->ResetTemplates();
    void* memory = Mem::Calloc(0x10);
    m_gameFlow = memory ? static_cast<Game::CGameFlow*>(memory)->Construct() : nullptr;
    memory = Mem::Calloc(0x39C);
    m_goalManager = memory ? static_cast<Game::CGoalManager*>(memory)->Construct() : nullptr;
    GameFunction<void(__cdecl*)(void*)>(0x0048C0A0)(GameVariable<void*>(0x006F0FB8));
    memory = Mem::Calloc(0x6AC);
    m_career = memory ? static_cast<Obj::CSkaterCareer*>(memory)->Construct() : nullptr;
    memory = Mem::Calloc(0x54);
    m_railManager = memory ? static_cast<Obj::CRailManager*>(memory)->Init() : nullptr;
    memory = Mem::Calloc(0x404);
    m_checksums = memory ? static_cast<Obj::ChecksumSet*>(memory)->Construct() : nullptr;
    TrickObjects()->RemoveAllClusters();
    m_levelLaunched = false;
    m_gameType = kFreeskate;
    memory = Mem::Calloc(0xC);
    m_splitscreenPreferences = memory ? static_cast<Prefs::Preferences*>(memory)->Init() : nullptr;
    m_splitscreenPreferences->Load(kDefaultSplitscreenPreferences);
    memory = Mem::Calloc(0x168);
    m_competition = memory ? static_cast<CCompetition*>(memory)->Construct(0) : nullptr;
    memory = Mem::Calloc(0x438);
    m_horse = memory ? static_cast<CHorse*>(memory)->Construct() : nullptr;
    memory = Mem::Calloc(0x12C);
    m_movies = memory ? static_cast<Obj::CMovieManager*>(memory)->Construct() : nullptr;
    memory = Mem::Calloc(0x12C);
    m_objectMovies = memory ? static_cast<Obj::CMovieManager*>(memory)->Construct() : nullptr;
    GameVariable<Records::CGameRecords*>(0x007CE4BC)->InitLevels(0x15);
    for (uint32_t i = 0; i < 8; ++i) {
        m_players[i].m_unknown0 = static_cast<int>(i); // (its index)
        m_players[i].m_vibration = true;
        m_players[i].m_autoKick = true;
        if (Obj::CSkater* skater = SkaterAt(i))
            reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterCorePhysics))[0x2D9] = 1;
        m_players[i].m_spinTaps = false;
        if (Obj::CSkater* skater = SkaterAt(i))
            reinterpret_cast<uint8_t*>(skater->GetComponent(kSkaterCorePhysics))[0x2DA] = 0;
    }
    m_script = nullptr;
    return this;
}

// 0x005663A0
void Skate::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(0x0064E674);
    GameFunction<void(__cdecl*)()>(0x00481340)();
    Terrain::ClearTerrainTypes();
    Script::RunScript(kUninitLightManager, nullptr, nullptr, false, nullptr);
    if (CCompetition* competition = m_competition) {
        competition->DestroyBody();
        Mem::Free(competition);
    }
    Mem::Free(m_horse);
    if (m_movies)
        reinterpret_cast<void(__thiscall*)(Obj::CMovieManager*, int)>(m_movies->m_vtable[0])(m_movies, 1);
    if (m_objectMovies)
        reinterpret_cast<void(__thiscall*)(Obj::CMovieManager*, int)>(m_objectMovies->m_vtable[0])(m_objectMovies, 1);
    if (Obj::CRailManager* rails = m_railManager) {
        rails->Cleanup();
        Mem::Free(rails);
    }
    if (m_career)
        reinterpret_cast<void(__thiscall*)(Obj::CSkaterCareer*, int)>(*reinterpret_cast<void***>(m_career)[0])(m_career, 1);
    reinterpret_cast<SkateNet*>(this)->DeleteNetworkTasks();
    if (m_gameFlow)
        reinterpret_cast<void(__thiscall*)(Game::CGameFlow*, int)>(*reinterpret_cast<void***>(m_gameFlow)[0])(m_gameFlow, 1);
    if (Game::CGoalManager* goals = m_goalManager) {
        goals->DestroyBody();
        Mem::Free(goals);
    }
    if (m_logicTask)
        reinterpret_cast<void(__thiscall*)(Tsk::BaseTask*, int)>(m_logicTask->m_vtable[2])(m_logicTask, 1);
    if (m_splitscreenPreferences)
        reinterpret_cast<void(__thiscall*)(Prefs::Preferences*, int)>(*reinterpret_cast<void***>(m_splitscreenPreferences)[0])(
            m_splitscreenPreferences, 1);
    Mem::Free(m_checksums);
    if (Script::CScript* script = m_script) {
        script->Destroy();
        Script::CScript::operator delete(script);
    }
    SkaterHeadDestroyBody(&m_skaters.m_node);
    Module::DestroyBody();
}

// 0x00567BA0
Skate* Skate::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00565750: the skater list's node's scalar deleting destructor (Lst::Node<CSkater>, vtable 0x0064E544).
Mem::ListNode* __fastcall Skate::DestroySkaterNode(Mem::ListNode* node, int, uint8_t flags)
{
    node->UnlinkAs(reinterpret_cast<void**>(0x0064E544));
    if (flags & 1)
        Mem::Free(node);
    return node;
}

// 0x00565FC0: the skater list's head's (Lst::Head<CSkater>, vtable 0x0064E5D4).
Mem::ListNode* __fastcall Skate::DestroySkaterHead(Mem::ListNode* head, int, uint8_t flags)
{
    SkaterHeadDestroyBody(head);
    if (flags & 1)
        Mem::Free(head);
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
        if (m_career->GetCheat(cheat))
            cheats |= cheat;
    return cheats;
}

} // namespace Mdl

REPLACE(0x005665C0, &Mdl::Skate::GetSkater);
REPLACE_UNCHECKED(0x00566520, Mdl::Skate::GetLocalSkaterStub); // ECX kept
REPLACE_UNCHECKED(0x00566600, Mdl::Skate::GetSkaterByIdStub); // ECX kept
REPLACE(0x00565790, &Mdl::Skate::RemoveSkater);
REPLACE(0x00567180, &Mdl::Skate::AddSkater);
REPLACE(0x00565FF0, &Mdl::Skate::GetNextUnusedSkaterHeapIndex);
REPLACE(0x00565BF0, &Mdl::Skate::SetSkaterShadow);
REPLACE(0x00567090, &Mdl::Skate::BindControllers);
REPLACE(0x00566100, &Mdl::Skate::SkipToRestartPoint);
REPLACE(0x00566370, &Mdl::Skate::MoveToRestart);
REPLACE(0x00566980, &Mdl::Skate::ResetSkaters);
REPLACE(0x00565A00, &Mdl::Skate::ChangeLevel);
REPLACE(0x005666A0, &Mdl::Skate::Cleanup);
REPLACE(0x005668A0, &Mdl::Skate::RestartLevel);
REPLACE(0x00566900, &Mdl::Skate::ResetLevel);
REPLACE(0x00566A60, &Mdl::Skate::LaunchGame);
REPLACE(0x00566BE0, &Mdl::Skate::CheckSkaterCollisions);
REPLACE(0x00566E70, Mdl::Skate::CFunc_Retry);
REPLACE(0x00566EA0, Mdl::Skate::CFunc_LaunchGame);
REPLACE(0x00566ED0, Mdl::Skate::CFunc_ResetLevel);
REPLACE(0x00566F40, &Mdl::Skate::UpdateRecords);
REPLACE(0x00567580, &Mdl::Skate::AllSkatersIdle);
REPLACE(0x00567690, &Mdl::Skate::FirstTrickStarted);
REPLACE(0x005676E0, &Mdl::Skate::FirstTrickCompleted);
REPLACE(0x00567BD0, &Mdl::Skate::SetSpinTaps);
REPLACE(0x00567C00, &Mdl::Skate::SetAutoKick);
REPLACE(0x00567C30, &Mdl::Skate::SetVibration);
REPLACE(0x00565ED0, &Mdl::Skate::RunScript);
REPLACE(0x00565F80, &Mdl::Skate::KillScript);
REPLACE(0x00565DA0, Mdl::Skate::DebugRails);
REPLACE(0x00564D30, Mdl::Skate::UpdateSuspendVisibility);
REPLACE(0x00567710, &Mdl::Skate::DoUpdate);
REPLACE(0x00568C90, Mdl::Skate::LogicTask);
REPLACE(0x00568DE0, &Mdl::Skate::Construct);
REPLACE(0x005663A0, &Mdl::Skate::DestroyBody);
REPLACE(0x00567BA0, &Mdl::Skate::Destroy);
REPLACE(0x00565750, Mdl::Skate::DestroySkaterNode);
REPLACE(0x00565FC0, Mdl::Skate::DestroySkaterHead);
REPLACE(0x00569580, &Mdl::Skate::BeginQueuedScripts);
REPLACE(0x00569590, &Mdl::Skate::EndQueuedScripts);
REPLACE(0x005695A0, &Mdl::Skate::GetCheats);
