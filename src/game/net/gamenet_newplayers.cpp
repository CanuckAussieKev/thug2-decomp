// The network game's joining players made real (GameNet::Manager): on a client, a NewPlayerInfo from the server
// becomes a player with its skater (AddNewPlayer) once its time has come (ClientAddNewPlayersTask); on the server the
// same (ServerAddNewPlayersTask) with the messages telling everyone about it (0x0053D9D0-0x0053EB70). See
// gamenet_manager.h.

#include <cstring>

#include "game/core/manager.h"
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
#include "hook/game.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (strcpy: the original's copies of a player's name)

namespace GameNet {

using namespace Detail;

namespace {

// NewPlayerInfo (0x44 bytes; constructor 0x005434D0, destructor 0x00543560) - the fields used here.
constexpr uint32_t kInfoObjId = 0x10;     // int8_t
constexpr uint32_t kInfoConn = 0x14;
constexpr uint32_t kInfoTime = 0x18;      // when it may be added (the app's +0x1410: its frame)
constexpr uint32_t kInfoProfile = 0x1C;   // CSkaterProfile*
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

Manager* TaskManager(void* task) { return Field<Manager*>(task, 0x1C); }
uint8_t* SkateModule() { return Skate(); }

// The skate module's list of skaters (+0x34): how many.
int SkaterCount(uint8_t* skate)
{
    int count = 0;
    for (auto* node = Field<Mem::ListNode*>(skate, 0x34); node->data != reinterpret_cast<void*>(-1) && node;
         node = node->next)
        ++count;
    return count;
}

} // namespace

// 0x0053E440
void Manager::AddNewPlayer(uint8_t* info)
{
    uint8_t* skate = SkateModule();
    Obj::CObject* skater = nullptr;
    int8_t objId = Field<int8_t>(info, kInfoObjId);
    uint32_t flags = Field<uint32_t>(info, kInfoFlags);
    if (!(flags & kNewObserver)) {
        bool splitScreen = CFuncs::CFunc_InSplitScreenGame(nullptr, nullptr) != 0;
        void* profile = Call<void*>(0x00564FC0, skate, splitScreen ? static_cast<int>(objId) : 0); // the skater's profile
        if (!InNetGame()) {
            skater = Call<Obj::CObject*>(0x00567180, skate, profile, true, static_cast<int>(objId), SkaterCount(skate));
        } else if (Field<uint32_t>(info, kInfoFlags) & kNewLocal) {
            skater = Call<Obj::CObject*>(0x00567180, skate, profile, true, static_cast<int>(objId), 0);
        } else {
            auto* skaters = reinterpret_cast<Mdl::Skate*>(skate);
            int count = Call<void*>(0x00566520, skate) ? skaters->GetNumSkaters() : (skaters->GetNumSkaters() + 1) & 7;
            skater = Call<Obj::CObject*>(0x00567180, skate, Field<void*>(info, kInfoProfile), false,
                                         static_cast<int>(objId), count);
            if (Field<int>(info, kInfoVehicle)) // its vehicle control type
                Field<int>(static_cast<Obj::CCompositeObject*>(skater)->GetComponent(kVehicleComponent), 0x1104) =
                    Field<int>(info, kInfoVehicle);
        }
    }
    uint8_t* player = PlayerByObjectId(static_cast<uint16_t>(static_cast<int16_t>(objId)));
    if (Field<uint32_t>(info, kInfoFlags) & kNewResync) { // the old player (or the local one) goes
        if (!player)
            player = LocalPlayer(this);
        if (player) {
            DestroyPlayer(player);
            player = nullptr;
        }
        Call(0x00566370, skate, skater, 0, 0);
    }
    if (!player) {
        player = NewPlayer(skater, Field<Net::Conn*>(info, kInfoConn), Field<uint32_t>(info, kInfoFlags) & kNewPlayerFlags);
        std::strcpy(reinterpret_cast<char*>(player + 0x38), reinterpret_cast<const char*>(info));
        Field<int>(player, 0x30) = Field<int>(info, kInfoTeam);
        Field<int>(player, 0x28) = Field<int>(info, kInfoProfileId);
        Field<int>(player, 0x2C) = Field<int>(info, kInfoRating);
        Field<int>(player, 0x48) = Field<int>(info, kInfoVehicle);
        Field<uint32_t>(player, 0x20) = Field<uint32_t>(info, kInfo38);
        Field<uint32_t>(player, 0x1C) = Field<uint32_t>(info, kInfo3C);
        auto* head = &Field<Mem::ListNode>(this, 0x11C);
        auto* node = reinterpret_cast<Mem::ListNode*>(player);
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
        if (Field<uint8_t>(info, kInfoFlags) & kNewHasCrown)
            AsPlayer(player)->MarkAsKing(true);
    }
    if (Field<uint32_t>(info, kInfoFlags) & kNewResync) {
        PrepareLocalFace();
        SendCustomTagTexture();
        GameVariable<Obj::CTrickObjectManager*>(0x007CE500)->RemoveAllClusters(); // the trick objects
        Run(kResyncedPlayers);
    }
    if (!IsLocalPlayer(player)) {
        auto* object = static_cast<Obj::CCompositeObject*>(skater);
        uint8_t* score = Field<uint8_t*>(object->GetComponent(kSkaterScore), 0x18);
        static_cast<Obj::CSkater*>(skater)->HideSkater(); // hidden
        Call(0x00561A80, score, Field<int>(info, kInfoScore));
    }
}

// 0x0053EA30
void __cdecl Manager::ClientAddNewPlayersTask(void* task)
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_skaterInfoHeap);
    Manager* manager = TaskManager(task);
    uint8_t* skate = SkateModule();
    Net::ConnSearch search = {reinterpret_cast<void**>(0x0064759C), nullptr};
    auto* node = Field<Mem::ListNode*>(manager, 0x13C);
    bool added = false;
    if (node->data != reinterpret_cast<void*>(-1) && node) {
        while (node) {
            Mem::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            auto* info = static_cast<uint8_t*>(node->data);
            if (Field<uint32_t>(info, kInfoTime) > Field<uint32_t>(manager->m_client, 0x1410))
                break; // (its time hasn't come: the rest wait too)
            manager->AddNewPlayer(info);
            reinterpret_cast<NewPlayerInfo*>(info)->DestroyBody(); // (its destructor)
            Mem::Manager::Get()->Free(info);
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(node))[0])(node, 1);
            added = true;
            node = next;
        }
        if (added) {
            for (int i = 0; i < 2; ++i) {
                if (Net::App* client = manager->ClientSlot(i))
                    if (Net::Conn* conn = client->FirstConnection(&search))
                        conn->UpdateCommTime(0); // (the time spent adding them isn't silence)
            }
            for (int i = 0; i < 8; ++i)
                if (void* skater = Call<void*>(0x005665C0, skate, i))
                    static_cast<Obj::CSkater*>(skater)->Reset27C();
            manager->SendReadyResponse();
        }
    }
    Mem::Manager::Get()->PopContext();
}

namespace {

// A player's details for the others (message 0x22, the client's HandleNewPlayer 0x0052DB80): a 0x30-byte header and
// its appearance (CSkaterProfile::WriteToBuffer, at most 0x1000 bytes).
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
    msg.team = static_cast<int8_t>(Field<int>(player, 0x30));
    msg.profileId = Field<int32_t>(player, 0x28);
    msg.rating = Field<int32_t>(player, 0x2C);
    msg.vehicleControlType = Field<int32_t>(player, 0x48);
    msg.score = 0;
    msg.value14 = 0;
    msg.value10 = 0;
    std::strcpy(msg.name, reinterpret_cast<const char*>(player + 0x38));
    int size = Field<Obj::CSkaterProfile*>(player, 0x4C)->WriteToBuffer(msg.appearance, kAppearanceSize, broadband ? 1 : 0);
    return static_cast<uint16_t>(size + kNewPlayerHeader);
}

void SendNewPlayer(Net::App* server, Net::MsgDesc& desc, uint8_t* to, NewPlayerMessage& msg, uint16_t length)
{
    desc.m_data = &msg;
    desc.m_length = length;
    desc.m_id = kMsgNewPlayer;
    desc.m_priority = kNormalPriority;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    desc.m_compression = kCompressionLzss;
    server->EnqueueMessage(PlayerHandle(to), &desc);
    desc.m_compression = 0;
}

void SendWord(Net::App* server, Net::MsgDesc& desc, uint8_t* to, uint8_t id, uint32_t* word)
{
    desc.m_data = word;
    desc.m_length = sizeof(*word);
    desc.m_id = id;
    desc.m_priority = kNormalPriority;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    server->EnqueueMessage(PlayerHandle(to), &desc);
}

} // namespace

// 0x0053D9D0
void __cdecl Manager::ServerAddNewPlayersTask(void* task)
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_skaterInfoHeap);
    Manager* manager = TaskManager(task);
    uint8_t* skate = SkateModule();
    Net::MsgDesc desc;
    desc.Construct();
    Net::Manager* net = Net::Manager::Instance();
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    auto* node = Field<Mem::ListNode*>(manager, 0x13C);
    if (node->data != reinterpret_cast<void*>(-1) && node) {
        while (node) {
            PlayerSearch search;
            Mem::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
            auto* info = static_cast<uint8_t*>(node->data);
            Net::App* server = manager->m_server;
            if (Field<uint32_t>(info, kInfoTime) > Field<uint32_t>(server, 0x1410))
                break; // (its time hasn't come: the rest wait too)
            auto* conn = Field<Net::Conn*>(info, kInfoConn);
            bool broadband = conn->GetBandwidthType() == 1 || net->GetConnectionType() == 1;
            Mem::Manager::Get()->PushContext(Mem::Manager::Get()->m_skaterInfoHeap);
            uint8_t* player = manager->NewPlayer(nullptr, conn, Field<uint32_t>(info, kInfoFlags));
            Mem::Manager::Get()->PopContext();
            std::strcpy(reinterpret_cast<char*>(player + 0x38), reinterpret_cast<const char*>(info));
            AsPlayer(player)->SetProfile(Field<Obj::CSkaterProfile*>(info, kInfoProfile)); // its appearance (a copy of the profile)
            Field<uint32_t>(player, 0x1D8) = Field<uint32_t>(info, 0x24);
            Field<int>(player, 0x28) = Field<int>(info, kInfoProfileId);
            Field<int>(player, 0x2C) = Field<int>(info, kInfoRating);
            Field<int>(player, 0x48) = Field<int>(info, kInfoVehicle);
            Field<uint32_t>(player, 0x20) = 0;
            Field<uint32_t>(player, 0x1C) = 0;
            auto* head = &Field<Mem::ListNode>(manager, 0x11C);
            auto* playerNode = reinterpret_cast<Mem::ListNode*>(player);
            playerNode->prev = head->prev;
            playerNode->next = head;
            head->prev->next = playerNode;
            head->prev = playerNode;
            uint32_t now = Tmr::GetTime();
            NewPlayerMessage msg;
            int8_t objId = Field<int8_t>(info, kInfoObjId);
            if (!IsLocalPlayer(player)) { // its own details (no appearance) after its important messages
                msg.objId = objId;
                msg.team = static_cast<int8_t>(Field<int>(player, 0x30));
                msg.profileId = Field<int32_t>(player, 0x28);
                msg.rating = Field<int32_t>(player, 0x2C);
                msg.vehicleControlType = Field<int32_t>(player, 0x48);
                msg.score = 0;
                msg.value14 = 0;
                msg.value10 = 0;
                std::strcpy(msg.name, reinterpret_cast<const char*>(info));
                msg.flags = 1;
                if (Field<uint32_t>(player, 0x1D0) & kNewResync)
                    msg.flags = 0x11;
                if (IsObserver(player))
                    msg.flags |= 4;
                desc.m_data = nullptr;
                desc.m_length = 0;
                desc.m_id = kMsgFlushImportant;
                desc.m_priority = kHighestPriority;
                server->EnqueueMessage(PlayerHandle(player), &desc);
                desc.m_data = &msg;
                desc.m_length = kNewPlayerHeader;
                desc.m_id = kMsgNewPlayer;
                desc.m_compression = kCompressionLzss;
                server->EnqueueMessage(PlayerHandle(player), &desc);
                desc.m_compression = 0;
            }
            if (!IsObserver(player)) { // the new player to every other remote player
                for (uint8_t* other = FirstPlayer(manager, &search, true); other; other = NextPlayer(manager, &search, true)) {
                    if (other == player || IsLocalPlayer(other))
                        continue;
                    uint16_t length = FillNewPlayerMessage(msg, player, objId, 0, false);
                    SendNewPlayer(server, desc, other, msg, length);
                }
            }
            if (Field<uint32_t>(player, 0x1D0) & kNewResync) { // the players of its own group to it
                for (uint8_t* other = FirstPlayer(manager, &search, false); other;
                     other = NextPlayer(manager, &search, false)) {
                    if (other == player || Field<uint32_t>(other, 0x1D8) != Field<uint32_t>(player, 0x1D8))
                        continue;
                    uint16_t length = FillNewPlayerMessage(msg, other, static_cast<int8_t>(SkaterId(PlayerSkater(other))),
                                                           Field<uint32_t>(other, 0x1D0) & kPlayerFlagsSent, broadband);
                    SendNewPlayer(server, desc, player, msg, length);
                }
            } else { // every playing player to it, the created park and its rails, then joined
                for (uint8_t* other = FirstPlayer(manager, &search, false); other;
                     other = NextPlayer(manager, &search, false)) {
                    if (other == player || IsLocalPlayer(player))
                        continue;
                    int8_t otherId = static_cast<int8_t>(SkaterId(PlayerSkater(other)));
                    msg.objId = otherId;
                    msg.flags = Field<uint32_t>(other, 0x1D0) & kPlayerFlagsSent;
                    msg.team = static_cast<int8_t>(Field<int>(other, 0x30));
                    msg.profileId = Field<int32_t>(other, 0x28);
                    msg.rating = Field<int32_t>(other, 0x2C);
                    msg.vehicleControlType = Field<int32_t>(other, 0x48);
                    msg.score = manager->GetPlayerScore(SkaterId(PlayerSkater(other))); // (its score)
                    msg.value14 = Field<uint32_t>(other, 0x20);
                    msg.value10 = Field<uint32_t>(other, 0x1C);
                    std::strcpy(msg.name, reinterpret_cast<const char*>(other + 0x38));
                    int size = Field<Obj::CSkaterProfile*>(other, 0x4C)->WriteToBuffer(msg.appearance, kAppearanceSize,
                                                                                       broadband ? 1 : 0);
                    SendNewPlayer(server, desc, player, msg, static_cast<uint16_t>(size + kNewPlayerHeader));
                }
                uint32_t level = manager->m_level;
                if (!IsLocalPlayer(player) && level == kLoadParkLevel) {
                    auto* park = Ed::CParkEditor::Instance();
                    park->WriteCompressedMapIfBuilt();
                    server->StreamMessage(PlayerHandle(player), kMsgParkMap, kParkMapSize,
                                          park->GetCompressedMapBuffer(false), "level data", kGroupGame, false, true);
                    Ed::CRailEditorComponent::Instance(); // (called twice, the first result unused)
                    void* rails = Call<void*>(0x004F45B0, Ed::CRailEditorComponent::Instance());
                    server->StreamMessage(PlayerHandle(player), kMsgRails, kRailsSize, rails, "rail data", kGroupGame,
                                          false, true);
                }
                SendWord(server, desc, player, kMsgJoinAccepted, &level);
            }
            if (!IsObserver(player)) { // its skater; every other player asked whether it's ready
                int count = IsLocalPlayer(player) && manager->InNetGame() ? 0 : SkaterCount(skate);
                bool localConn = Field<Net::Conn*>(player, 0x18)->IsLocal();
                auto* skater = Call<Obj::CObject*>(0x00567180, skate, Field<void*>(info, kInfoProfile), localConn,
                                                   static_cast<int>(objId), count);
                AsPlayer(player)->SetSkater(static_cast<Obj::CSkater*>(skater));
                if (!IsLocalPlayer(player))
                    static_cast<Obj::CSkater*>(skater)->HideSkater(); // hidden
                for (uint8_t* other = FirstPlayer(manager, &search, true); other; other = NextPlayer(manager, &search, true)) {
                    if (other == player)
                        continue;
                    SendWord(server, desc, other, kMsgReadyQuery, &now);
                    if (!IsLocalPlayer(other) && !IsObserver(other)) {
                        static_cast<Obj::CSkater*>(PlayerSkater(other))->Reset27C(); // reset
                        AsPlayer(other)->AwaitReadyResponse(now);
                    }
                }
            }
            SendWord(server, desc, player, kMsgReadyQuery, &now);
            AsPlayer(player)->AwaitReadyResponse(now);
            reinterpret_cast<NewPlayerInfo*>(info)->DestroyBody(); // (its destructor)
            Mem::Manager::Get()->Free(info);
            reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(node))[0])(node, 1);
            node = next;
        }
    }
    Mem::Manager::Get()->PopContext();
    Manager* global = GameVariable<Manager*>(0x007CCE14);
    global->UpdateLobbyServer();
    if (GameVariable<bool>(0x007CCA82) && global->InNetGame() && GameVariable<bool>(0x007CCE1C))
        GameFunction<void(__cdecl*)(int)>(0x005FBAD0)(0); // qr2_send_statechanged
}

} // namespace GameNet

REPLACE(0x0053D9D0, GameNet::Manager::ServerAddNewPlayersTask);
REPLACE(0x0053E440, &GameNet::Manager::AddNewPlayer);
REPLACE(0x0053EA30, GameNet::Manager::ClientAddNewPlayersTask);
