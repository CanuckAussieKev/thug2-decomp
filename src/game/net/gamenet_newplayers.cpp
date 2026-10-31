// The network game's joining players made real (OnlinePlay::Hub): on a client, a JoinerRecord from the server
// becomes a player with its skater (AddNewPlayer) once its time has come (ClientAddNewPlayersTask); on the server the
// same (ServerAddNewPlayersTask) with the messages telling everyone about it (0x0053D9D0-0x0053EB70). See
// gamenet_manager.h.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/net/gamespy.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/ed/park_editor.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/net/net_lists.h"
#include "game/net/net_manager.h"
#include "game/obj/trick_objects.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/sys/timer.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/game_cfuncs.h"
#include "game/ed/rail_editor.h"
#include "game/core/code_address.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

#pragma warning(disable : 4996) // (strcpy: the original's copies of a player's name)

namespace OnlinePlay {

using namespace Detail;

namespace {

// JoinerRecord (0x44 bytes; constructor 0x005434D0, destructor 0x00543560) - the fields used here.
constexpr uint32_t kInfoObjId = 0x10;     // int8_t
constexpr uint32_t kInfoConn = 0x14;
constexpr uint32_t kInfoTime = 0x18;      // when it may be added (the app's +0x1410: its frame)
constexpr uint32_t kInfoProfile = 0x1C;   // CRiderIdentity*
constexpr uint32_t kInfoFlags = 0x20;
constexpr uint32_t kInfoTeam = 0x28;
constexpr uint32_t kInfoProfileId = 0x2C;
constexpr uint32_t kInfoRating = 0x30;
constexpr uint32_t kInfoScore = 0x34;
constexpr uint32_t kInfo38 = 0x38;
constexpr uint32_t kInfo3C = 0x3C;
constexpr uint32_t kInfoVehicle = 0x40;
// Its flags.
constexpr uint32_t kNewLocal = 0x01;
constexpr uint32_t kNewObserver = 0x04;
constexpr uint32_t kNewResync = 0x10;      // the player again (after a level change)
constexpr uint32_t kNewHasCrown = 0x80;
constexpr uint32_t kNewPlayerFlags = 0x27805; // the flags a player keeps

constexpr uint32_t kSkaterScore = 0xD262802F;     // the score component
constexpr uint32_t kVehicleComponent = 0x18223FD6; // (its vehicle control type at +0x1104)
constexpr uint32_t kResyncedPlayers = 0x1D02CE3B; // (run after a resync)

Hub* TaskManager(void* task) { return Prop<Hub*>(task, 0x1C); }
uint8_t* SkateModule() { return Skate(); }

// The skate module's list of skaters (+0x34): how many.
int SkaterCount(uint8_t* skate)
{
    int count = 0;
    for (auto* node = Prop<MemSys::ListNode*>(skate, 0x34); node->data != reinterpret_cast<void*>(-1) && node;
         node = node->next)
        ++count;
    return count;
}

} // namespace

// 0x0053E440
void Hub::AddNewPlayer(uint8_t* info)
{
    uint8_t* skate = SkateModule();
    GameObjects::CEntity* skater = nullptr;
    int8_t objId = Prop<int8_t>(info, kInfoObjId);
    uint32_t flags = Prop<uint32_t>(info, kInfoFlags);
    if (!(flags & kNewObserver)) {
        bool splitScreen = ScriptCommands::CFunc_InSplitScreenGame(nullptr, nullptr) != 0;
        void* profile = Call<void*>(CodeAddress(static_cast<GameObjects::CRiderIdentity* (GameModules::Skate::*)(int)>(&GameModules::Skate::FetchAccount)), skate, splitScreen ? static_cast<int>(objId) : 0); // the skater's profile
        if (!InNetGame()) {
            skater = Call<GameObjects::CEntity*>(CodeAddress(&GameModules::Skate::AddSkater), skate, profile, true, static_cast<int>(objId), SkaterCount(skate));
        } else if (Prop<uint32_t>(info, kInfoFlags) & kNewLocal) {
            skater = Call<GameObjects::CEntity*>(CodeAddress(&GameModules::Skate::AddSkater), skate, profile, true, static_cast<int>(objId), 0);
        } else {
            auto* skaters = reinterpret_cast<GameModules::Skate*>(skate);
            int count = Call<void*>(CodeAddress(GameModules::Skate::GetLocalSkaterStub), skate) ? skaters->CountRiders() : (skaters->CountRiders() + 1) & 7;
            skater = Call<GameObjects::CEntity*>(CodeAddress(&GameModules::Skate::AddSkater), skate, Prop<void*>(info, kInfoProfile), false,
                                         static_cast<int>(objId), count);
            if (Prop<int>(info, kInfoVehicle)) // its vehicle control type
                Prop<int>(static_cast<GameObjects::CGameEntity*>(skater)->FetchModule(kVehicleComponent), 0x1104) =
                    Prop<int>(info, kInfoVehicle);
        }
    }
    uint8_t* player = PlayerByObjectId(static_cast<uint16_t>(static_cast<int16_t>(objId)));
    if (Prop<uint32_t>(info, kInfoFlags) & kNewResync) { // the old player (or the local one) goes
        if (!player)
            player = LocalPlayer(this);
        if (player) {
            RemovePlayerRecord(player);
            player = nullptr;
        }
        Call(CodeAddress(&GameModules::Skate::TeleportToSpawn), skate, skater, 0, 0);
    }
    if (!player) {
        player = NewPlayer(skater, Prop<NetLink::Conn*>(info, kInfoConn), Prop<uint32_t>(info, kInfoFlags) & kNewPlayerFlags);
        std::strcpy(reinterpret_cast<char*>(player + 0x38), reinterpret_cast<const char*>(info));
        Prop<int>(player, 0x30) = Prop<int>(info, kInfoTeam);
        Prop<int>(player, 0x28) = Prop<int>(info, kInfoProfileId);
        Prop<int>(player, 0x2C) = Prop<int>(info, kInfoRating);
        Prop<int>(player, 0x48) = Prop<int>(info, kInfoVehicle);
        Prop<uint32_t>(player, 0x20) = Prop<uint32_t>(info, kInfo38);
        Prop<uint32_t>(player, 0x1C) = Prop<uint32_t>(info, kInfo3C);
        auto* head = &Prop<MemSys::ListNode>(this, 0x11C);
        auto* node = reinterpret_cast<MemSys::ListNode*>(player);
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
        if (Prop<uint8_t>(info, kInfoFlags) & kNewHasCrown)
            AsPlayer(player)->GiveCrown(true);
    }
    if (Prop<uint32_t>(info, kInfoFlags) & kNewResync) {
        PrepareLocalFace();
        SendCustomTagTexture();
        GameObjects::g_trickObjectManager->RemoveAllClusters(); // the trick objects
        Run(kResyncedPlayers);
    }
    if (!IsOwnPlayer(player)) {
        auto* object = static_cast<GameObjects::CGameEntity*>(skater);
        uint8_t* score = Prop<uint8_t*>(object->FetchModule(kSkaterScore), 0x18);
        static_cast<GameObjects::CRider*>(skater)->ConcealSkater(); // hidden
        Call(CodeAddress(&GameModules::PointTally::SetGrandTotal), score, Prop<int>(info, kInfoScore));
    }
}

// 0x0053EA30
void __cdecl Hub::ClientAddNewPlayersTask(void* task)
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_skaterInfoHeap);
    Hub* manager = TaskManager(task);
    uint8_t* skate = SkateModule();
    NetLink::ConnSearch search = {NetLink::kConnSearchVtable, nullptr};
    auto* node = Prop<MemSys::ListNode*>(manager, 0x13C);
    bool added = false;
    if (node->data != reinterpret_cast<void*>(-1) && node) {
        while (node) {
            MemSys::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            auto* info = static_cast<uint8_t*>(node->data);
            if (Prop<uint32_t>(info, kInfoTime) > Prop<uint32_t>(manager->m_clientApp, 0x1410))
                break; // (its time hasn't come: the rest wait too)
            manager->AddNewPlayer(info);
            reinterpret_cast<JoinerRecord*>(info)->DestroyBody(); // (its destructor)
            MemSys::Hub::Pull()->Dealloc(info);
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(node))[0])(node, 1);
            added = true;
            node = next;
        }
        if (added) {
            for (int i = 0; i < 2; ++i) {
                if (NetLink::AppHost* client = manager->ClientSlot(i))
                    if (NetLink::Conn* conn = client->BeginConnWalk(&search))
                        conn->StampLastHeard(0); // (the time spent adding them isn't silence)
            }
            for (int i = 0; i < 8; ++i)
                if (void* skater = Call<void*>(CodeAddress(&GameModules::Skate::GetRiderAt), skate, i))
                    static_cast<GameObjects::CRider*>(skater)->Reset27C();
            manager->SendReadyResponse();
        }
    }
    MemSys::Hub::Pull()->LeaveHeapScope();
}

namespace {

// A player's details for the others (message 0x22, the client's HandleNewPlayer 0x0052DB80): a 0x30-byte header and
// its appearance (CRiderIdentity::EmitIntoBuffer, at most 0x1000 bytes).
struct NewPlayerMessage {
    int32_t profileId;      // +0x00
    int32_t rating;         // +0x04
    uint32_t flags;         // +0x08
    int32_t score;          // +0x0C
    uint32_t value10;       // +0x10 (the player's +0x1C)
    uint32_t value14;       // +0x14 (the player's +0x20)
    int8_t objId;           // +0x18
    int8_t team;            // +0x19
    uint8_t pad1A[2];       // (never written)
    int32_t vehicleControlType; // +0x1C
    char name[kNameSize];   // +0x20
    uint8_t appearance[kAppearanceSize]; // +0x30
};
static_assert(offsetof(NewPlayerMessage, objId) == 0x18 && offsetof(NewPlayerMessage, name) == 0x20 &&
              offsetof(NewPlayerMessage, appearance) == 0x30);
constexpr uint16_t kNewPlayerHeader = 0x30;

constexpr uint8_t kMsgNewPlayer = 0x22;
constexpr uint8_t kMsgReadyQuery = 0x25;
constexpr uint8_t kMsgJoinAccepted = 0x53;
constexpr uint8_t kMsgParkMap = 0x6C;
constexpr uint8_t kMsgFlushImportant = 0x79;
constexpr uint8_t kMsgRails = 0x88;
constexpr uint16_t kParkMapSize = 0x3A98;
constexpr uint16_t kRailsSize = 0x1132;
constexpr uint8_t kCompressionLzss = 2;
constexpr uint32_t kLoadParkLevel = 0xB664035D; // load_sk5ed_gameplay (the created park)
constexpr uint32_t kPlayerFlagsSent = 0x27880;  // the flags a player's details carry
constexpr int kNormalPriority = 0x80;
constexpr int kHighestPriority = 0xFF;

// The player's details (its object id given), its appearance written into the message; the message's length.
uint16_t FillNewPlayerMessage(NewPlayerMessage& msg, uint8_t* player, int8_t objId, uint32_t flags, bool broadband)
{
    msg.objId = objId;
    msg.flags = flags;
    msg.team = static_cast<int8_t>(Prop<int>(player, 0x30));
    msg.profileId = Prop<int32_t>(player, 0x28);
    msg.rating = Prop<int32_t>(player, 0x2C);
    msg.vehicleControlType = Prop<int32_t>(player, 0x48);
    msg.score = 0;
    msg.value14 = 0;
    msg.value10 = 0;
    std::strcpy(msg.name, reinterpret_cast<const char*>(player + 0x38));
    int size = Prop<GameObjects::CRiderIdentity*>(player, 0x4C)->EmitIntoBuffer(msg.appearance, kAppearanceSize, broadband ? 1 : 0);
    return static_cast<uint16_t>(size + kNewPlayerHeader);
}

void SendNewPlayer(NetLink::AppHost* server, NetLink::NetMessageInfo& desc, uint8_t* to, NewPlayerMessage& msg, uint16_t length)
{
    desc.m_content = &msg;
    desc.m_byteCount = length;
    desc.m_ident = kMsgNewPlayer;
    desc.m_rank = kNormalPriority;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    desc.m_compression = kCompressionLzss;
    server->PutOnOutbox(PlayerHandle(to), &desc);
    desc.m_compression = 0;
}

void SendWord(NetLink::AppHost* server, NetLink::NetMessageInfo& desc, uint8_t* to, uint8_t id, uint32_t* word)
{
    desc.m_content = word;
    desc.m_byteCount = sizeof(*word);
    desc.m_ident = id;
    desc.m_rank = kNormalPriority;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    server->PutOnOutbox(PlayerHandle(to), &desc);
}

} // namespace

// 0x0053D9D0
void __cdecl Hub::ServerAddNewPlayersTask(void* task)
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_skaterInfoHeap);
    Hub* manager = TaskManager(task);
    uint8_t* skate = SkateModule();
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    NetLink::Hub* net = NetLink::Hub::Sole();
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    auto* node = Prop<MemSys::ListNode*>(manager, 0x13C);
    if (node->data != reinterpret_cast<void*>(-1) && node) {
        while (node) {
            PlayerSearch search;
            MemSys::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            auto* info = static_cast<uint8_t*>(node->data);
            NetLink::AppHost* server = manager->m_host;
            if (Prop<uint32_t>(info, kInfoTime) > Prop<uint32_t>(server, 0x1410))
                break; // (its time hasn't come: the rest wait too)
            auto* conn = Prop<NetLink::Conn*>(info, kInfoConn);
            bool broadband = conn->GetLinkSpeedClass() == 1 || net->GetNetLinkKind() == 1;
            MemSys::Hub::Pull()->EnterHeapScope(MemSys::Hub::Pull()->m_skaterInfoHeap);
            uint8_t* player = manager->NewPlayer(nullptr, conn, Prop<uint32_t>(info, kInfoFlags));
            MemSys::Hub::Pull()->LeaveHeapScope();
            std::strcpy(reinterpret_cast<char*>(player + 0x38), reinterpret_cast<const char*>(info));
            AsPlayer(player)->SetProfile(Prop<GameObjects::CRiderIdentity*>(info, kInfoProfile)); // its appearance (a copy of the profile)
            Prop<uint32_t>(player, 0x1D8) = Prop<uint32_t>(info, 0x24);
            Prop<int>(player, 0x28) = Prop<int>(info, kInfoProfileId);
            Prop<int>(player, 0x2C) = Prop<int>(info, kInfoRating);
            Prop<int>(player, 0x48) = Prop<int>(info, kInfoVehicle);
            Prop<uint32_t>(player, 0x20) = 0;
            Prop<uint32_t>(player, 0x1C) = 0;
            auto* head = &Prop<MemSys::ListNode>(manager, 0x11C);
            auto* playerNode = reinterpret_cast<MemSys::ListNode*>(player);
            playerNode->prev = head->prev;
            playerNode->next = head;
            head->prev->next = playerNode;
            head->prev = playerNode;
            uint32_t now = FrameClock::GetTime();
            NewPlayerMessage msg;
            int8_t objId = Prop<int8_t>(info, kInfoObjId);
            if (!IsOwnPlayer(player)) { // its own details (no appearance) after its important messages
                msg.objId = objId;
                msg.team = static_cast<int8_t>(Prop<int>(player, 0x30));
                msg.profileId = Prop<int32_t>(player, 0x28);
                msg.rating = Prop<int32_t>(player, 0x2C);
                msg.vehicleControlType = Prop<int32_t>(player, 0x48);
                msg.score = 0;
                msg.value14 = 0;
                msg.value10 = 0;
                std::strcpy(msg.name, reinterpret_cast<const char*>(info));
                msg.flags = 1;
                if (Prop<uint32_t>(player, 0x1D0) & kNewResync)
                    msg.flags = 0x11;
                if (IsObserver(player))
                    msg.flags |= 4;
                desc.m_content = nullptr;
                desc.m_byteCount = 0;
                desc.m_ident = kMsgFlushImportant;
                desc.m_rank = kHighestPriority;
                server->PutOnOutbox(PlayerHandle(player), &desc);
                desc.m_content = &msg;
                desc.m_byteCount = kNewPlayerHeader;
                desc.m_ident = kMsgNewPlayer;
                desc.m_compression = kCompressionLzss;
                server->PutOnOutbox(PlayerHandle(player), &desc);
                desc.m_compression = 0;
            }
            if (!IsObserver(player)) { // the new player to every other remote player
                for (uint8_t* other = FirstPlayer(manager, &search, true); other; other = NextPlayer(manager, &search, true)) {
                    if (other == player || IsOwnPlayer(other))
                        continue;
                    uint16_t length = FillNewPlayerMessage(msg, player, objId, 0, false);
                    SendNewPlayer(server, desc, other, msg, length);
                }
            }
            if (Prop<uint32_t>(player, 0x1D0) & kNewResync) { // the players of its own group to it
                for (uint8_t* other = FirstPlayer(manager, &search, false); other;
                     other = NextPlayer(manager, &search, false)) {
                    if (other == player || Prop<uint32_t>(other, 0x1D8) != Prop<uint32_t>(player, 0x1D8))
                        continue;
                    uint16_t length = FillNewPlayerMessage(msg, other, static_cast<int8_t>(SkaterId(PlayerSkater(other))),
                                                           Prop<uint32_t>(other, 0x1D0) & kPlayerFlagsSent, broadband);
                    SendNewPlayer(server, desc, player, msg, length);
                }
            } else { // every playing player to it, the created park and its rails, then joined
                for (uint8_t* other = FirstPlayer(manager, &search, false); other;
                     other = NextPlayer(manager, &search, false)) {
                    if (other == player || IsOwnPlayer(player))
                        continue;
                    int8_t otherId = static_cast<int8_t>(SkaterId(PlayerSkater(other)));
                    msg.objId = otherId;
                    msg.flags = Prop<uint32_t>(other, 0x1D0) & kPlayerFlagsSent;
                    msg.team = static_cast<int8_t>(Prop<int>(other, 0x30));
                    msg.profileId = Prop<int32_t>(other, 0x28);
                    msg.rating = Prop<int32_t>(other, 0x2C);
                    msg.vehicleControlType = Prop<int32_t>(other, 0x48);
                    msg.score = manager->GetEntrantPoints(SkaterId(PlayerSkater(other))); // (its score)
                    msg.value14 = Prop<uint32_t>(other, 0x20);
                    msg.value10 = Prop<uint32_t>(other, 0x1C);
                    std::strcpy(msg.name, reinterpret_cast<const char*>(other + 0x38));
                    int size = Prop<GameObjects::CRiderIdentity*>(other, 0x4C)->EmitIntoBuffer(msg.appearance, kAppearanceSize,
                                                                                       broadband ? 1 : 0);
                    SendNewPlayer(server, desc, player, msg, static_cast<uint16_t>(size + kNewPlayerHeader));
                }
                uint32_t level = manager->m_level;
                if (!IsOwnPlayer(player) && level == kLoadParkLevel) {
                    auto* park = ParkEditing::CParkBuilderMode::Sole();
                    park->WriteCompressedMapIfBuilt();
                    server->SendBulkMessage(PlayerHandle(player), kMsgParkMap, kParkMapSize,
                                          park->GetPackedParkData(false), "level data", kGroupGame, false, true);
                    ParkEditing::CRailAuthoring::Sole(); // (called twice, the first result unused)
                    void* rails = Call<void*>(CodeAddress(&ParkEditing::CRailAuthoring::GetPacked), ParkEditing::CRailAuthoring::Sole());
                    server->SendBulkMessage(PlayerHandle(player), kMsgRails, kRailsSize, rails, "rail data", kGroupGame,
                                          false, true);
                }
                SendWord(server, desc, player, kMsgJoinAccepted, &level);
            }
            if (!IsObserver(player)) { // its skater; every other player asked whether it's ready
                int count = IsOwnPlayer(player) && manager->InNetGame() ? 0 : SkaterCount(skate);
                bool localConn = Prop<NetLink::Conn*>(player, 0x18)->IsHostLocal();
                auto* skater = Call<GameObjects::CEntity*>(CodeAddress(&GameModules::Skate::AddSkater), skate, Prop<void*>(info, kInfoProfile), localConn,
                                                   static_cast<int>(objId), count);
                AsPlayer(player)->BindRider(static_cast<GameObjects::CRider*>(skater));
                if (!IsOwnPlayer(player))
                    static_cast<GameObjects::CRider*>(skater)->ConcealSkater(); // hidden
                for (uint8_t* other = FirstPlayer(manager, &search, true); other; other = NextPlayer(manager, &search, true)) {
                    if (other == player)
                        continue;
                    SendWord(server, desc, other, kMsgReadyQuery, &now);
                    if (!IsOwnPlayer(other) && !IsObserver(other)) {
                        static_cast<GameObjects::CRider*>(PlayerSkater(other))->Reset27C(); // reset
                        AsPlayer(other)->AwaitReadyResponse(now);
                    }
                }
            }
            SendWord(server, desc, player, kMsgReadyQuery, &now);
            AsPlayer(player)->AwaitReadyResponse(now);
            reinterpret_cast<JoinerRecord*>(info)->DestroyBody(); // (its destructor)
            MemSys::Hub::Pull()->Dealloc(info);
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(node))[0])(node, 1);
            node = next;
        }
    }
    MemSys::Hub::Pull()->LeaveHeapScope();
    Hub* global = g_gameNetManager;
    global->UpdateLobbyServer();
    if (g_netEnabled && global->InNetGame() && g_queryReporting)
        GameSpy::Qr2SendStateChanged(nullptr); // qr2_send_statechanged
}

} // namespace OnlinePlay
