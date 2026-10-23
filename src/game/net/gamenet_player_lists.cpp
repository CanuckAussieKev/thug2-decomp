// The network manager's player records (GameNet::Manager, 0x00536820-0x00537F40): the players' and joining players'
// lists - walking, finding and removing them.

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4996) // (strcpy / strcat: the original's own copies into its fixed buffers)

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/buddy_status.h"
#include "game/net/net_client.h"
#include "game/net/net_lists.h"
#include "game/net/net_manager.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/skate/profile_manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lists.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint32_t kPlayers = 0x11C;       // Lst::Head<PlayerInfo>
constexpr uint32_t kNewPlayers = 0x130;    // Lst::Head<NewPlayerInfo>
constexpr uint32_t kQueuedScripts = 0x144; // QueuedScript (a head of its own type)

constexpr uint32_t kNewPlayerObjectId = 0x10; // NewPlayerInfo: its object id (a signed byte)
constexpr uint32_t kNewPlayerConn = 0x14;     // NewPlayerInfo: its connection
constexpr uint32_t kPlayerFlags = 0x1D0;      // PlayerInfo: flags
constexpr uint32_t kSkaterListFirst = 0x34;   // the skate module's skater list's first node (its head is at +0x28)

// Messages.
constexpr uint8_t kMsgPanelMessage = 0x3F;
constexpr uint8_t kMsgToggleProSet = 0x70;

// Checksums.
constexpr uint32_t kSkaterScore = 0xD262802F;           // SkaterScore (the component)
constexpr uint32_t kText = 0xC4745838;                  // text
constexpr uint32_t kId = 0x40C698AF;                    // id
constexpr uint32_t kNetPanelMsg = 0x85BE419B;           // net_panel_msg
constexpr uint32_t kMsgTime = 0xEDD17FEB;               // msg_time
constexpr uint32_t kCreateNetPanelMessage = 0xD9BB372E; // create_net_panel_message
constexpr uint32_t kPreRunQueuedScripts = 0xD4DAFEB2;   // PreRunQueuedScripts
constexpr uint32_t kPostRunQueuedScripts = 0xE1BDBE73;  // PostRunQueuedScripts

const char* const kStringSlot = reinterpret_cast<const char*>(0x0064CA08); // "%s0"

// The panel message (message 0x3F, 44 bytes).
struct PanelMessage {
    uint32_t properties; // +0x00 the CRC of the properties' name
    uint32_t text;       // +0x04 the global text
    int time;            // +0x08
    char string1[16];    // +0x0C (the bytes after its end: the original's stack)
    char string2[16];    // +0x1C
};
static_assert(sizeof(PanelMessage) == 0x2C, "layout must match the original");

Mem::ListNode* PlayerList(Manager* manager) { return &Field<Mem::ListNode>(manager, kPlayers); }
Mem::ListNode* NewPlayerList(Manager* manager) { return &Field<Mem::ListNode>(manager, kNewPlayers); }
Mem::ListNode* QueuedScriptList(Manager* manager) { return &Field<Mem::ListNode>(manager, kQueuedScripts); }
bool IsHead(const Mem::ListNode* node) { return reinterpret_cast<intptr_t>(node->data) == -1; }
// The node after `node`; null at the list's end (the head).
Mem::ListNode* NextNode(const Mem::ListNode* node) { return IsHead(node->next) ? nullptr : node->next; }
uint8_t* DataOf(const Mem::ListNode* node) { return node ? static_cast<uint8_t*>(node->data) : nullptr; }
// The nodes from `node` to the list's end (the original's loop: the head's mark tested before a null node).
unsigned int CountNodes(const Mem::ListNode* node)
{
    unsigned int count = 0;
    while (!IsHead(node) && node) {
        node = node->next;
        ++count;
    }
    return count;
}
// --- 0x00539610-0x0053A580's ---
constexpr uint32_t kNewPlayerSize = 0x44;      // NewPlayerInfo (constructor 0x005434D0)
constexpr uint32_t kNewPlayerProfile = 0x1C;   // NewPlayerInfo: its skater profile
constexpr uint32_t kNewPlayerFlags = 0x20;     // (4 observing, 8 will play, 0x10, 0x18 ...)
constexpr uint32_t kNewPlayerTeam = 0x28;
constexpr uint32_t kPlayerConnection = 0x18;   // PlayerInfo: its connection
constexpr uint32_t kPlayerProfileInfo = 0x4C;  // PlayerInfo: its skater profile
constexpr uint32_t kPlayerFace = 0x1E8;        // PlayerInfo: its face texture data (a byte, then kFaceSize)
constexpr uint16_t kFaceSize = 0x4498;
constexpr uint8_t kMsgFace = 0x7E;
constexpr uint8_t kGroupFace = 9;
const char* const kFaceDesc = reinterpret_cast<const char*>(0x0064CA2C); // "Face Data"
constexpr uint8_t kMsgTagOwner = 0x96;
constexpr uint8_t kMsgTagTexture = 0x97;
constexpr uint8_t kMsgTagPalette = 0x98;
constexpr uint8_t kMsgTagLogos = 0x9A;
constexpr uint8_t kGroupTag = 10;
const char* const kTagPaletteDesc = reinterpret_cast<const char*>(0x0064C974); // "CAG Palette"
const char* const kTagTextureDesc = reinterpret_cast<const char*>(0x0064C968); // "CAG Texture"
constexpr uint32_t kHatLogo = 0x5B288E0B;      // hat_logo
constexpr uint32_t kFrontLogo = 0x23A1DF80;    // front_logo
constexpr uint32_t kBackLogo = 0xD0C656A2;     // back_logo
constexpr uint32_t kDeckGraphic = 0x07CB4FA9;  // deck_graphic
constexpr uint32_t kNetMessageJoining = 0xA41ECAAF;     // net_message_joining
constexpr uint32_t kNetMessageObserving = 0x8A7E0A70;   // net_message_observing
constexpr uint32_t kNetMessageJoinPending = 0xF33706D5; // net_message_join_pending
constexpr uint32_t kNetCtf = 0x6C5FF266;       // netctf
constexpr uint32_t kTrgCtfRed = 0x36EA35EB;    // TRG_CTF_Red
constexpr uint32_t kTrgCtfBlue = 0x06270B12;   // TRG_CTF_Blue
constexpr uint32_t kTrgCtfGreen = 0x3DB516B0;  // TRG_CTF_Green
constexpr uint32_t kTrgCtfYellow = 0x85E38F38; // TRG_CTF_Yellow
constexpr uint32_t kSkeleton = 0x222756D5;     // Skeleton (the component)
constexpr uint32_t kBoneHead = 0xDDEC28AF;     // Bone_Head
constexpr float kQuarterTurn = 90.0f;          // (CreateRotateXMatrix's angle, as the original passes it)

// A joining player who will play: flags 0x18, or not observing.
bool WillPlay(const uint8_t* info)
{
    uint32_t flags = Field<uint32_t>(info, kNewPlayerFlags);
    return (flags & 0x18) || !(flags & 4);
}
Net::Conn* PlayerConn(const uint8_t* player) { return Field<Net::Conn*>(player, kPlayerConnection); }
// An object deleted through its vtable (slot 2, with flag 1).
void DeleteVirtual(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(object))[2])(object, 1);
}
// 0x00485CB0: whether the skater profile's appearance has the CAS part `part` set.
bool ProfileHasPart(void* profile, uint32_t part)
{
    return GameFunction<bool(__cdecl*)(void*, uint32_t)>(0x00485CB0)(profile, part);
}
// A custom tag texture's parts (Nx::CTexture's virtual slots 25-28).
void* TexturePixels(void* texture) { return (*reinterpret_cast<void* (__thiscall***)(void*)>(texture))[25](texture); }
uint16_t TextureSize(void* texture)
{
    return static_cast<uint16_t>((*reinterpret_cast<int(__thiscall***)(void*)>(texture))[26](texture));
}
void* TexturePalette(void* texture) { return (*reinterpret_cast<void* (__thiscall***)(void*)>(texture))[27](texture); }
uint16_t TexturePaletteSize(void* texture)
{
    return static_cast<uint16_t>((*reinterpret_cast<int(__thiscall***)(void*)>(texture))[28](texture));
}
// D3DXMatrixMultiply (the D3DX library's, stdcall): out = a * b.
void D3DXMatrixMultiply(void* out, const void* a, const void* b)
{
    GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C)(out, a, b);
}

// The first player (observers too) for whom the PlayerInfo member `test` is true (0 / 1 in EAX: tested as AL).
void* FindPlayer(Manager* manager, int (PlayerInfo::*test)())
{
    for (Mem::ListNode* node = NextNode(PlayerList(manager)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if ((AsPlayer(player)->*test)() != 0)
            return player;
    }
    return nullptr;
}

// A node deleted through its vtable (slot 0, the scalar deleting destructor).
void DeleteNode(Mem::ListNode* node) { reinterpret_cast<void(__thiscall*)(Mem::ListNode*, unsigned int)>(node->vtable[0])(node, 1); }

// A joining player's record deleted (NewPlayerInfo's destructor body 0x00543560, then freed).
void DeleteNewPlayer(void* player)
{
    static_cast<NewPlayerInfo*>(player)->DestroyBody();
    Mem::Manager::Get()->Free(player);
}

} // namespace

// 0x00536820
void Manager::RemoveAllNewPlayers()
{
    Mem::ListNode* node = NewPlayerList(this)->next;
    if (IsHead(node))
        return;
    while (node) {
        void* player = node->data;
        Mem::ListNode* next = IsHead(node->next) ? nullptr : node->next;
        if (m_server)
            m_server->TerminateConnection(Field<Net::Conn*>(player, kNewPlayerConn));
        DeleteNode(node);
        if (player)
            DeleteNewPlayer(player);
        node = next;
    }
}

// 0x00536B10
void Manager::RemoveNewPlayer(void* player)
{
    Mem::ListNode* node = NewPlayerList(this)->next;
    // (the original tests for the list's end before it tests for a null node)
    if (IsHead(node) || !node)
        return;
    while (node->data != player) {
        node = node->next;
        if (IsHead(node) || !node)
            return;
    }
    Net::Conn* conn = Field<Net::Conn*>(player, kNewPlayerConn);
    if (m_server)
        m_server->TerminateConnection(conn);
    else if (conn)
        m_client->TerminateConnection(conn);
    DeleteNode(node);
    DeleteNewPlayer(player);
}

// 0x00537010
void* Manager::NextPlayerInfo(void* search, bool observers)
{
    auto* s = static_cast<PlayerSearch*>(search);
    auto* node = NextNode(static_cast<Mem::ListNode*>(s->m_node));
    s->m_node = node;
    uint8_t* player = DataOf(node);
    if (!observers && player && IsObserver(player))
        return NextPlayerInfo(search, false);
    return player;
}

// 0x005372C0
void* Manager::FirstPlayerInfo(void* search, bool observers)
{
    auto* s = static_cast<PlayerSearch*>(search);
    Mem::ListNode* head = PlayerList(this);
    s->m_node = head;
    Mem::ListNode* node = NextNode(head);
    s->m_node = node;
    uint8_t* player = DataOf(node);
    if (!observers && player && IsObserver(player))
        return NextPlayerInfo(search, false);
    return player;
}

// 0x00537070
void* Manager::FirstNewPlayerInfo(void* search)
{
    auto* s = static_cast<NewPlayerSearch*>(search);
    Mem::ListNode* head = NewPlayerList(this);
    s->m_node = head;
    Mem::ListNode* node = NextNode(head);
    s->m_node = node;
    return DataOf(node);
}

// (no address: the lists' generic next, folded at 0x004573D0 - see the header)
void* Manager::NextNewPlayerInfo(void* search)
{
    auto* s = static_cast<NewPlayerSearch*>(search);
    Mem::ListNode* next = static_cast<Mem::ListNode*>(s->m_node)->next;
    s->m_node = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
    return s->m_node ? next->data : nullptr;
}

// 0x005370A0
void* Manager::GetNewPlayerInfoByObjectId(uint16_t id)
{
    Mem::ListNode* node = NextNode(NewPlayerList(this));
    for (uint8_t* player = DataOf(node); player; player = DataOf(node)) {
        if (static_cast<int>(Field<int8_t>(player, kNewPlayerObjectId)) == static_cast<int>(id))
            return player;
        node = NextNode(node);
    }
    return nullptr;
}

// 0x00537140
void Manager::SetReadyToPlay(bool ready)
{
    if (static_cast<uint8_t>(ready) == *reinterpret_cast<uint8_t*>(&m_readyToPlay))
        return;
    m_readyToPlay = ready;
    uint8_t* skate = Detail::Skate();
    unsigned int count = CountNodes(Field<Mem::ListNode*>(skate, kSkaterListFirst));
    for (unsigned int i = 0; i < count; ++i) {
        auto* skater = Call<void***>(0x005665C0, skate, i);
        if (skater) // (the skater's slot 12 when ready, 11 when not)
            reinterpret_cast<void(__thiscall*)(void*)>((*skater)[ready ? 12 : 11])(skater);
    }
}

// 0x00537320
void* Manager::GetLocalPlayer() { return FindPlayer(this, &PlayerInfo::IsLocalPlayer); }

// 0x005373D0
void* Manager::GetServerPlayer() { return FindPlayer(this, &PlayerInfo::IsServerPlayer); }

// 0x00537480
void* Manager::GetKingOfTheHill() { return FindPlayer(this, &PlayerInfo::IsKing); }

// 0x00537530
int Manager::CountOtherPlayers()
{
    PlayerSearch search;
    int count = 0;
    for (void* player = FirstPlayerInfo(&search, false); player; player = NextPlayerInfo(&search, false)) {
        if (!static_cast<PlayerInfo*>(player)->IsFullyIn()) // (flags 0x21)
            ++count;
    }
    return count;
}

// 0x00537620
void Manager::ResetPlayers()
{
    for (Mem::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        Field<uint32_t>(player, kPlayerFlags) &= ~0x00020100u;
        Field<uint32_t>(player, 0x20) = 0;
        Field<uint32_t>(player, 0x1C) = 0;
        Field<uint32_t>(player, 0x34) = 0;
        auto* skater = reinterpret_cast<Obj::CCompositeObject*>(PlayerSkater(player));
        if (skater) {
            auto* score = skater->GetComponent(kSkaterScore); // (not checked for null, as in the original)
            if (Field<void*>(score, 0x18))
                Call(0x00564A10, Field<void*>(score, 0x18));
        }
    }
}

// 0x005376F0
void Manager::CreateNetPanelMessage(bool broadcast, uint32_t text, const char* string1, const char* string2, void*,
                                    const char* properties, bool important, int time)
{
    if (broadcast) {
        Net::MsgDesc desc;
        desc.Construct();
        PanelMessage message;
        message.string1[0] = 0;
        message.string2[0] = 0;
        message.text = text;
        message.time = time;
        message.properties = Crc::GenerateCRCFromString(properties);
        if (string1)
            std::strcpy(message.string1, string1);
        if (string2)
            std::strcpy(message.string2, string2);
        desc.m_data = &message;
        desc.m_length = sizeof(PanelMessage);
        desc.m_id = kMsgPanelMessage;
        if (important) {
            desc.m_queue = Net::MsgDesc::kQueueSequenced;
            desc.m_groupId = kGroupGame;
        }
        for (Mem::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node))
            m_server->EnqueueMessage(PlayerHandle(player), &desc);
        return;
    }
    Script::CStruct* params = NewParams();
    const char* format = Script::GetString(text);
    char copy[256];
    char line[256];
    std::strcpy(copy, format);
    const char* shown = format;
    // "%s0" in the text stands for string 1.
    if (char* slot = std::strstr(copy, kStringSlot)) {
        *slot = 0;
        std::strcpy(line, copy);
        std::strcat(line, string1);
        std::strcat(line, slot + 3);
        shown = line;
    }
    params->AddString(kText, Script::ESYMBOLTYPE_STRING, shown);
    params->AddComponentOfType(kId, Script::ESYMBOLTYPE_NAME, kNetPanelMsg);
    params->AddInteger(kMsgTime, time);
    Run(kCreateNetPanelMessage, params);
    DeleteParams(params);
}

// 0x00537A10
void Manager::QueueScript(int node, uint32_t objectId, uint32_t script)
{
    Mem::ListNode* head = QueuedScriptList(this);
    for (Mem::ListNode* n = NextNode(head); auto* queued = reinterpret_cast<QueuedScript*>(DataOf(n)); n = NextNode(n)) {
        if (queued->m_script == script)
            return;
    }
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_networkHeap);
    auto* queued = static_cast<QueuedScript*>(Mem::Calloc(sizeof(QueuedScript)));
    if (queued) { // (a null one is written to anyway, as in the original)
        queued->data = queued;
        queued->priority = 0;
        queued->next = queued;
        queued->prev = queued;
        queued->vtable = kQueuedScriptVtable;
    }
    queued->m_node = node;
    queued->m_objectId = objectId;
    queued->m_script = script;
    // Onto the end of the list.
    queued->prev = head->prev;
    queued->next = head;
    head->prev->next = queued;
    head->prev = queued;
    Mem::Manager::Get()->PopContext();
}

// 0x00537B30
void Manager::ClearQueuedScripts()
{
    Mem::ListNode* node = NextNode(QueuedScriptList(this));
    for (uint8_t* queued = DataOf(node); queued;) {
        node = NextNode(node);
        uint8_t* next = DataOf(node);
        DeleteNode(reinterpret_cast<Mem::ListNode*>(queued));
        queued = next;
    }
}

// 0x00537D90
void Manager::LaunchQueuedScripts()
{
    Mem::Manager* memory = Mem::Manager::Get();
    auto* sound = GameVariable<Sfx::CSfxManager*>(0x006F85A8);
    uint8_t* skate = Detail::Skate();
    memory->PushContext(memory->m_bottomUpHeap);
    Call(0x00569580, skate);
    float volume = Sfx::GetMainVolume();
    sound->SetMainVolume(0.0f);
    Run(kPreRunQueuedScripts);
    Mem::ListNode* node = NextNode(QueuedScriptList(this));
    for (auto* queued = reinterpret_cast<QueuedScript*>(DataOf(node)); queued;) {
        node = NextNode(node);
        auto* next = reinterpret_cast<QueuedScript*>(DataOf(node));
        Script::CScript* script =
            Script::SpawnScript(queued->m_script, nullptr, 0, nullptr, queued->m_node, 0, false, false, false, false);
        Obj::CObject* object = GameVariable<Obj::CBaseManager*>(0x006F0FB8)->GetObjectByID(queued->m_objectId);
        script->m_object.Set(object);
        while (script->Update() != Script::ESCRIPTRETURNVAL_FINISHED) {
        }
        Script::KillSpawnedScript(script);
        DeleteNode(queued);
        queued = next;
    }
    Run(kPostRunQueuedScripts);
    sound->SetMainVolume(volume);
    Call(0x00569590, skate);
    Mem::Manager::Get()->PopContext();
}

// 0x00537F40
void Manager::ToggleProSet(uint8_t bit, uint32_t param)
{
    Net::MsgDesc desc;
    desc.Construct();
    struct {
        uint32_t param;
        uint8_t bit;
        uint8_t pad[3]; // (the original's stack)
    } message = {param, bit, {0, 0, 0}};
    m_proSetFlags ^= 1u << (bit & 31);
    desc.m_length = sizeof(message);
    desc.m_groupId = kGroupGame;
    desc.m_data = &message;
    desc.m_id = kMsgToggleProSet;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    Net::App* server = m_server;
    for (Mem::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if (!IsLocalPlayer(player))
            server->EnqueueMessage(PlayerHandle(player), &desc);
    }
}

// 0x00539880
int Manager::CountTeamPlayers(int team)
{
    PlayerSearch search;
    int count = 0;
    for (auto* player = static_cast<uint8_t*>(FirstPlayerInfo(&search, false)); player;
         player = static_cast<uint8_t*>(NextPlayerInfo(&search, false))) {
        if (PlayerTeam(player) == team)
            ++count;
    }
    for (Mem::ListNode* node = NextNode(NewPlayerList(this)); uint8_t* info = DataOf(node); node = NextNode(node)) {
        if (WillPlay(info) && Field<int>(info, kNewPlayerTeam) == team)
            ++count;
    }
    return count;
}

// 0x00539B00
int Manager::CountPlayers()
{
    // The players' skater ids (zeroed first: the original's check below reads the byte after the last one noted).
    uint8_t ids[12] = {};
    int noted = 0;
    int count = 0;
    for (Mem::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if (!IsObserver(player) || (Field<uint8_t>(player, kPlayerFlags) & 8)) {
            if (Obj::CObject* skater = PlayerSkater(player))
                ids[noted++] = static_cast<uint8_t>(SkaterId(skater));
            ++count;
        }
    }
    for (Mem::ListNode* node = NextNode(NewPlayerList(this)); uint8_t* info = DataOf(node); node = NextNode(node)) {
        if (!WillPlay(info))
            continue;
        bool listed = false;
        for (int i = 0; i < noted && !listed; ++i)
            listed = Field<uint8_t>(info, kNewPlayerObjectId) == ids[noted]; // (ids[noted], as in the original)
        if (!listed)
            ++count;
    }
    return count;
}

// 0x00539C70
int Manager::CountObservers()
{
    int count = 0;
    for (Mem::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if (IsObserver(player) && !AsPlayer(player)->IsPendingObserver() && (!IsLocalPlayer(player) || m_hostMode == 0))
            ++count;
    }
    for (Mem::ListNode* node = NextNode(NewPlayerList(this)); uint8_t* info = DataOf(node); node = NextNode(node)) {
        uint32_t flags = Field<uint32_t>(info, kNewPlayerFlags);
        if ((flags & 4) && !(flags & 8))
            ++count;
    }
    return count;
}

// 0x005399F0
void Manager::DisconnectClients()
{
    Net::Manager* net = Net::Manager::Instance();
    for (int i = 0; i < 2; ++i) {
        if (!ClientSlot(i))
            continue;
        if (i == 0 && !m_client->IsLocal())
            m_clientAddNewPlayersTask->Remove();
        net->DestroyApp(ClientSlot(i));
        ClientSlot(i) = nullptr;
    }
    if (m_observerInputHandler) {
        DeleteVirtual(m_observerInputHandler);
        m_observerInputHandler = nullptr;
    }
    GameVariable<uint8_t>(0x007CCDF8) = 0;
    if (GameVariable<bool>(0x007CCE1C)) {
        GameFunction<void(__cdecl*)(int)>(0x005FBB20)(0);
        GameVariable<bool>(0x007CCE1C) = false;
    }
    GameFunction<void(__cdecl*)(int)>(0x005F9900)(1003);
    Field<Tsk::BaseTask*>(this, 0xD4)->Remove();
    Field<Tsk::BaseTask*>(this, 0xC8)->Remove();
    Field<Tsk::BaseTask*>(this, 0xEC)->Remove();
    ClearQueuedScripts();
    if (m_readyToPlay) {
        m_readyToPlay = false;
        uint8_t* skate = Detail::Skate();
        unsigned int count = CountNodes(Field<Mem::ListNode*>(skate, kSkaterListFirst));
        for (unsigned int i = 0; i < count; ++i) {
            auto* skater = Call<void***>(0x005665C0, skate, i);
            if (skater)
                reinterpret_cast<void(__thiscall*)(void*)>((*skater)[11])(skater);
        }
    }
}

// 0x00539DA0
void Manager::SendTagTexturesTo(void* target)
{
    auto* player = static_cast<uint8_t*>(target);
    PlayerSearch search;
    if (Net::Manager::Instance()->GetConnectionType() == Net::Manager::kConnectionModem || IsLocalPlayer(player))
        return;
    for (auto* other = static_cast<uint8_t*>(FirstPlayerInfo(&search, false)); other;
         other = static_cast<uint8_t*>(NextPlayerInfo(&search, false))) {
        void* texture = AsPlayer(other)->GetGraffitiTexture(); // its custom tag texture
        if (!texture || other == player || PlayerConn(player)->GetBandwidthType() == 1 || !AsPlayer(other)->HasCustomTag())
            continue;
        // (the logos are read from the receiving player's profile, as in the original)
        void* profile = Field<void*>(player, kPlayerProfileInfo);
        uint32_t logos = ProfileHasPart(profile, kHatLogo) ? 1 : 0;
        if (ProfileHasPart(profile, kFrontLogo))
            logos |= 2;
        if (ProfileHasPart(profile, kBackLogo))
            logos |= 4;
        if (ProfileHasPart(profile, kDeckGraphic))
            logos |= 8;
        Net::MsgDesc desc;
        desc.Construct();
        uint8_t skaterId = static_cast<uint8_t>(SkaterId(PlayerSkater(other)));
        desc.m_data = &skaterId;
        desc.m_id = kMsgTagOwner;
        desc.m_length = 1;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupTag;
        m_server->EnqueueMessage(PlayerHandle(player), &desc);
        void* palette = TexturePalette(texture);
        m_server->StreamMessageToConn(PlayerConn(player), kMsgTagPalette, TexturePaletteSize(texture), palette,
                                      kTagPaletteDesc, kGroupTag, false, true);
        void* pixels = TexturePixels(texture);
        m_server->StreamMessageToConn(PlayerConn(player), kMsgTagTexture, TextureSize(texture), pixels, kTagTextureDesc,
                                      kGroupTag, false, true);
        desc.m_data = &logos;
        desc.m_id = kMsgTagLogos;
        desc.m_length = 4;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupTag;
        m_server->EnqueueMessage(PlayerHandle(player), &desc);
    }
}

// 0x0053A040
bool Manager::PrepareLocalFace()
{
    if (Net::Manager::Instance()->GetConnectionType() == Net::Manager::kConnectionModem)
        return false;
    void* profile = Call<void*>(0x00564FC0, Detail::Skate(), 0);
    void* face = Field<void*>(profile, 0x54); // its face texture (0x00576E60)
    if (!face || !Call<bool>(0x00487510, face))
        return false;
    Mem::Manager* memory = Mem::Manager::Get();
    if (m_flags & 1) {
        auto* local = static_cast<uint8_t*>(GetLocalPlayer());
        memory->PushContext(memory->m_netMiscHeap);
        Field<uint8_t*>(local, kPlayerFace) = static_cast<uint8_t*>(Mem::New(kFaceSize + 1));
        Mem::Manager::Get()->PopContext();
        Field<uint8_t*>(local, kPlayerFace)[0] = 0;
        Call(0x00487440, face, Field<uint8_t*>(local, kPlayerFace) + 1, kFaceSize);
    } else {
        Net::ConnSearch search;
        search.m_vtable = Net::kConnSearchVtable;
        search.m_node = nullptr;
        if (m_client->FirstConnection(&search)->GetBandwidthType() != 0)
            return false;
        memory->PushContext(memory->m_networkHeap);
        void* data = Mem::New(kFaceSize);
        Mem::Manager::Get()->PopContext();
        Call(0x00487440, face, data, kFaceSize);
        static_cast<Net::Client*>(m_client)->StreamMessageToServer(kMsgFace, kFaceSize, data, kFaceDesc, kGroupFace,
                                                                    false, false);
        Mem::Free(data);
    }
    return true;
}

// 0x0053A1B0
void Manager::SendFacesTo(void* target)
{
    auto* player = static_cast<uint8_t*>(target);
    PlayerSearch search;
    if (Net::Manager::Instance()->GetConnectionType() == Net::Manager::kConnectionModem)
        return;
    for (auto* other = static_cast<uint8_t*>(FirstPlayerInfo(&search, false)); other;
         other = static_cast<uint8_t*>(NextPlayerInfo(&search, false))) {
        if (other == player || PlayerConn(player)->GetBandwidthType() == 1)
            continue;
        if (!AsPlayer(other)->GetFaceTextureData() && !(IsLocalPlayer(other) && PrepareLocalFace()))
            continue;
        m_server->StreamMessageToConn(PlayerConn(player), kMsgFace, kFaceSize + 1, AsPlayer(other)->GetFaceTextureData(),
                                      kFaceDesc, kGroupFace, false, true);
    }
}

// 0x0053A310
void Manager::AddJoiningPlayer(void* message, int timeout)
{
    auto* info = static_cast<uint8_t*>(message);
    Mem::ListNode* head = NewPlayerList(this);
    for (Mem::ListNode* node = NextNode(head); uint8_t* listed = DataOf(node); node = NextNode(node)) {
        if (Field<uint8_t>(listed, kNewPlayerObjectId) == Field<uint8_t>(info, kNewPlayerObjectId))
            return;
    }
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_networkHeap);
    void* recordMemory = Mem::Calloc(kNewPlayerSize);
    auto* record = reinterpret_cast<uint8_t*>(recordMemory ? static_cast<NewPlayerInfo*>(recordMemory)->Construct() : nullptr);
    std::strcpy(reinterpret_cast<char*>(record), reinterpret_cast<const char*>(info));
    Field<uint32_t>(record, 0x14) = Field<uint32_t>(info, 0x14);
    Field<uint8_t>(record, kNewPlayerObjectId) = Field<uint8_t>(info, kNewPlayerObjectId);
    for (uint32_t offset : {0x20u, 0x28u, 0x2Cu, 0x30u, 0x34u, 0x38u, 0x3Cu, 0x40u})
        Field<uint32_t>(record, offset) = Field<uint32_t>(info, offset);
    uint32_t flags = Field<uint32_t>(record, kNewPlayerFlags);
    if ((flags & 0x10) && (m_flags & 1))
        Field<int>(record, 0x24) = m_server->m_frameCount;
    if (!(flags & 4) || (flags & 8)) {
        // The appearance, through a buffer.
        if (!GameVariable<bool>(0x007CC979))
            Mem::EnsureManager();
        auto* buffer = static_cast<uint8_t*>(Mem::Manager::Get()->Allocate(0x1000, 1, nullptr));
        Field<Obj::CSkaterProfile*>(info, kNewPlayerProfile)->WriteToBuffer(buffer, 0x1000, 0);
        Field<Obj::CSkaterProfile*>(record, kNewPlayerProfile)->ReadFromBuffer(buffer);
        Mem::Manager::Get()->Free(buffer);
    }
    if (m_flags & 1) {
        flags = Field<uint32_t>(record, kNewPlayerFlags);
        Field<int>(record, 0x18) = m_server->m_frameCount + timeout;
        if (!(flags & 1)) {
            const char* name = reinterpret_cast<const char*>(info);
            if (flags & 4)
                CreateNetPanelMessage(true, (flags & 8) ? kNetMessageJoinPending : kNetMessageObserving, name, nullptr,
                                      nullptr, nullptr, false, 2000);
            else
                CreateNetPanelMessage(false, kNetMessageJoining, name, nullptr, nullptr, nullptr, false, 2000);
        }
    } else {
        Field<int>(record, 0x18) = m_client->m_frameCount + timeout;
    }
    // Its node, onto the end of the list.
    auto* node = static_cast<Mem::ListNode*>(Mem::Calloc(sizeof(Mem::ListNode)));
    if (node) { // (a null one is written to anyway, as in the original)
        node->vtable = kNewPlayerNodeVtable;
        node->data = record;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
    Mem::Manager::Get()->PopContext();
}

// 0x0053A580
void* Manager::NextCamPlayer()
{
    PlayerSearch search;
    auto canWatch = [](void* player) {
        return !static_cast<PlayerInfo*>(player)->IsSurveying() && !static_cast<PlayerInfo*>(player)->IsEliminated();
    };
    if (m_camPlayer) {
        void* player = FirstPlayerInfo(&search, false);
        while (player && player != m_camPlayer)
            player = NextPlayerInfo(&search, false);
        if (player) {
            // The ones after it; past the end, from the start again.
            for (player = NextPlayerInfo(&search, false); player; player = NextPlayerInfo(&search, false)) {
                if (canWatch(player))
                    return player;
            }
            m_camPlayer = nullptr;
            return NextCamPlayer();
        }
    }
    for (void* player = FirstPlayerInfo(&search, false); player; player = NextPlayerInfo(&search, false)) {
        if (canWatch(player))
            return player;
    }
    return nullptr;
}

// 0x00539610 (a task's code: the manager's flag task): in capture the flag, each flag a player (no observers)
// carries (flags 0x7800) put on the carrier's head - the flag object of the team it belongs to (TRG_CTF_Red / Blue /
// Green / Yellow; none: the task stops for the frame) gets the head bone's matrix, turned 90 degrees about x and
// raised 12, then the skater's own matrix; its position the bone's offset transformed, plus the skater's position.
void __cdecl CarriedFlagsTask(void*)
{
    Manager* manager = GameVariable<Manager*>(0x007CCE14);
    PlayerSearch search;
    if (GameType() != kNetCtf)
        return;
    for (auto* player = static_cast<uint8_t*>(manager->FirstPlayerInfo(&search, false)); player;
         player = static_cast<uint8_t*>(manager->NextPlayerInfo(&search, false))) {
        if (!AsPlayer(player)->IsCarryingFlag()) // (carries a flag)
            continue;
        uint32_t name;
        switch (AsPlayer(player)->GetFlagTeam()) {
        case 0: name = kTrgCtfRed; break;
        case 1: name = kTrgCtfBlue; break;
        case 2: name = kTrgCtfGreen; break;
        case 3: name = kTrgCtfYellow; break;
        default: name = 0; break;
        }
        auto* flag = reinterpret_cast<uint8_t*>(Obj::FindObject(name));
        if (!flag)
            return;
        auto* skater = reinterpret_cast<Obj::CCompositeObject*>(PlayerSkater(player));
        // (the component's +0x18, CSkeletonComponent::mp_skeleton, through the getter folded at 0x005480F0)
        auto* skeleton = reinterpret_cast<void*>(reinterpret_cast<FoldedGetters*>(skater->GetComponent(kSkeleton))->Word18());
        auto* matrix = reinterpret_cast<Mth::Matrix*>(flag + 0x6C);
        Call(0x004A36E0, skeleton, kBoneHead, matrix); // the head bone's matrix
        Mth::Matrix turn;
        D3DXMatrixMultiply(matrix, &Mth::CreateRotateXMatrix(turn, kQuarterTurn), matrix);
        float raise[4] = {0.0f, 12.0f, 0.0f, 0.0f};
        Call(0x0048A260, matrix, raise); // (moved along its own axes)
        float offset[4];
        std::memcpy(offset, flag + 0x9C, sizeof(offset)); // its translation row, taken out
        float identityRow[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        std::memcpy(flag + 0x9C, identityRow, sizeof(identityRow));
        auto* skaterMatrix = reinterpret_cast<Mth::Matrix*>(reinterpret_cast<uint8_t*>(skater) + 0x6C);
        D3DXMatrixMultiply(matrix, matrix, skaterMatrix);
        Mth::Vector moved;
        const Mth::Vector& out = skaterMatrix->Transform(moved, *reinterpret_cast<const Mth::Vector*>(offset));
        float* position = reinterpret_cast<float*>(flag + 0x4C);
        std::memcpy(position, &out, 16);
        const float* skaterPosition = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(skater) + 0x4C);
        for (int i = 0; i < 4; ++i)
            position[i] = skaterPosition[i] + position[i];
    }
}

// 0x005395C0 (no `this`): the manager released - its reference count (0x007CCE18) down, or (the last) destroyed.
void __cdecl ReleaseManager()
{
    uint32_t& references = GameVariable<uint32_t>(0x007CCE18);
    if (references > 1) {
        --references;
        return;
    }
    Manager* manager = GameVariable<Manager*>(0x007CCE14);
    if (manager) {
        manager->DestroyBody();
        Mem::Manager::Get()->Free(manager);
    }
    GameVariable<Manager*>(0x007CCE14) = nullptr;
    references = 0;
}

REPLACE(0x00539880, &Manager::CountTeamPlayers);
REPLACE(0x00539B00, &Manager::CountPlayers);
REPLACE(0x00539C70, &Manager::CountObservers);
REPLACE(0x005399F0, &Manager::DisconnectClients);
REPLACE(0x00539DA0, &Manager::SendTagTexturesTo);
REPLACE(0x0053A040, &Manager::PrepareLocalFace);
REPLACE(0x0053A1B0, &Manager::SendFacesTo);
REPLACE(0x0053A310, &Manager::AddJoiningPlayer);
REPLACE(0x0053A580, &Manager::NextCamPlayer);
REPLACE(0x00539610, CarriedFlagsTask);
REPLACE(0x005395C0, ReleaseManager);
REPLACE(0x00536820, &Manager::RemoveAllNewPlayers);
REPLACE(0x00536B10, &Manager::RemoveNewPlayer);
REPLACE(0x00537010, &Manager::NextPlayerInfo);
REPLACE(0x005372C0, &Manager::FirstPlayerInfo);
REPLACE(0x00537070, &Manager::FirstNewPlayerInfo);
REPLACE(0x005370A0, &Manager::GetNewPlayerInfoByObjectId);
REPLACE(0x00537140, &Manager::SetReadyToPlay);
REPLACE(0x00537320, &Manager::GetLocalPlayer);
REPLACE(0x005373D0, &Manager::GetServerPlayer);
REPLACE(0x00537480, &Manager::GetKingOfTheHill);
REPLACE(0x00537530, &Manager::CountOtherPlayers);
REPLACE(0x00537620, &Manager::ResetPlayers);
REPLACE(0x005376F0, &Manager::CreateNetPanelMessage);
REPLACE(0x00537A10, &Manager::QueueScript);
REPLACE(0x00537B30, &Manager::ClearQueuedScripts);
REPLACE(0x00537D90, &Manager::LaunchQueuedScripts);
REPLACE(0x00537F40, &Manager::ToggleProSet);

} // namespace GameNet
