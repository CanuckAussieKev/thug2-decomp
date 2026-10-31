// The network manager's player records (OnlinePlay::Hub, 0x00536820-0x00537F40): the players' and joining players'
// lists - walking, finding and removing them.

#include <cstdint>
#include <initializer_list>
#include "game/core/code_address.h"
#include "game/gfx/d3dx.h"
#include <cstring>

#pragma warning(disable : 4996) // (strcpy / strcat: the original's own copies into its fixed buffers)

#include "game/core/checksums.h"
#include "game/net/gamespy.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task_list.h"
#include "game/maths/mat4.h"
#include "game/maths/vec4.h"
#include "game/net/buddy_status.h"
#include "game/net/gamenet_game_cfuncs.h"
#include "game/net/net_client.h"
#include "game/net/net_lists.h"
#include "game/net/net_manager.h"
#include "game/obj/entity.h"
#include "game/obj/entity_hub.h"
#include "game/skate/profile_manager.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/sound/sfx.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_lists.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_conn.h"
#include "game/skate/cas_options.h"
#include "game/obj/compositemgr.h"
#include "game/goals/objective.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/point_tally.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/rig.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

namespace OnlinePlay {

using namespace Detail;

namespace {

constexpr uint32_t kPlayers = 0x11C;       // ListKit::Foremost<PlayerRecord>
constexpr uint32_t kNewPlayers = 0x130;    // ListKit::Foremost<JoinerRecord>
constexpr uint32_t kQueuedScripts = 0x144; // QueuedScript (a head of its own type)

constexpr uint32_t kNewPlayerObjectId = 0x10; // JoinerRecord: its object id (a signed byte)
constexpr uint32_t kNewPlayerConn = 0x14;     // JoinerRecord: its connection
constexpr uint32_t kPlayerFlags = 0x1D0;      // PlayerRecord: flags
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

const char* const kStringSlot = "%s0"; // "%s0"

// The panel message (message 0x3F, 44 bytes).
struct PanelMessage {
    uint32_t properties; // +0x00 the CRC of the properties' name
    uint32_t text;       // +0x04 the global text
    int time;            // +0x08
    char string1[16];    // +0x0C (the bytes after its end: the original's stack)
    char string2[16];    // +0x1C
};
static_assert(sizeof(PanelMessage) == 0x2C, "layout must match the original");

MemSys::ListNode* PlayerList(Hub* manager) { return &Prop<MemSys::ListNode>(manager, kPlayers); }
MemSys::ListNode* NewPlayerList(Hub* manager) { return &Prop<MemSys::ListNode>(manager, kNewPlayers); }
MemSys::ListNode* QueuedScriptList(Hub* manager) { return &Prop<MemSys::ListNode>(manager, kQueuedScripts); }
bool IsHead(const MemSys::ListNode* node) { return reinterpret_cast<intptr_t>(node->data) == -1; }
// The node after `node`; null at the list's end (the head).
MemSys::ListNode* NextNode(const MemSys::ListNode* node) { return IsHead(node->next) ? nullptr : node->next; }
uint8_t* DataOf(const MemSys::ListNode* node) { return node ? static_cast<uint8_t*>(node->data) : nullptr; }
// The nodes from `node` to the list's end (the original's loop: the head's mark tested before a null node).
unsigned int TallyNodes(const MemSys::ListNode* node)
{
    unsigned int count = 0;
    while (!IsHead(node) && node) {
        node = node->next;
        ++count;
    }
    return count;
}
// --- 0x00539610-0x0053A580's ---
constexpr uint32_t kNewPlayerSize = 0x44;      // JoinerRecord (constructor 0x005434D0)
constexpr uint32_t kNewPlayerProfile = 0x1C;   // JoinerRecord: its skater profile
constexpr uint32_t kNewPlayerFlags = 0x20;     // (4 observing, 8 will play, 0x10, 0x18 ...)
constexpr uint32_t kNewPlayerTeam = 0x28;
constexpr uint32_t kPlayerConnection = 0x18;   // PlayerRecord: its connection
constexpr uint32_t kPlayerProfileInfo = 0x4C;  // PlayerRecord: its skater profile
constexpr uint32_t kPlayerFace = 0x1E8;        // PlayerRecord: its face texture data (a byte, then kFaceSize)
constexpr uint16_t kFaceSize = 0x4498;
constexpr uint8_t kMsgFace = 0x7E;
constexpr uint8_t kGroupFace = 9;
const char* const kFaceDesc = "Face Data"; // "Face Data"
constexpr uint8_t kMsgTagOwner = 0x96;
constexpr uint8_t kMsgTagTexture = 0x97;
constexpr uint8_t kMsgTagPalette = 0x98;
constexpr uint8_t kMsgTagLogos = 0x9A;
constexpr uint8_t kGroupTag = 10;
const char* const kTagPaletteDesc = "CAG Palette"; // "CAG Palette"
const char* const kTagTextureDesc = "CAG Texture"; // "CAG Texture"
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
constexpr float kQuarterTurn = 90.0f;          // (MakePitchMatrix's angle, as the original passes it)

// A joining player who will play: flags 0x18, or not observing.
bool WillPlay(const uint8_t* info)
{
    uint32_t flags = Prop<uint32_t>(info, kNewPlayerFlags);
    return (flags & 0x18) || !(flags & 4);
}
NetLink::Conn* PlayerConn(const uint8_t* player) { return Prop<NetLink::Conn*>(player, kPlayerConnection); }
// An object deleted through its vtable (slot 2, with flag 1).
void DeleteVirtual(void* object)
{
    reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(object))[2])(object, 1);
}
// 0x00485CB0: whether the skater profile's appearance has the CAS part `part` set.
bool ProfileHasPart(void* profile, uint32_t part)
{
    return GameObjects::PartIsCreatedGraphic(profile, part);
}
// A custom tag texture's parts (Engine3D::CBitmapTexture's virtual slots 25-28).
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
    D3dx::MatrixMultiply(out, a, b);
}

// The first player (observers too) for whom the PlayerRecord member `test` is true (0 / 1 in EAX: tested as AL).
void* FindPlayer(Hub* manager, int (PlayerRecord::*test)())
{
    for (MemSys::ListNode* node = NextNode(PlayerList(manager)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if ((AsPlayer(player)->*test)() != 0)
            return player;
    }
    return nullptr;
}

// A node deleted through its vtable (slot 0, the scalar deleting destructor).
void UnlinkListNode(MemSys::ListNode* node) { reinterpret_cast<void(__thiscall*)(MemSys::ListNode*, unsigned int)>(node->vtable[0])(node, 1); }

// A joining player's record deleted (JoinerRecord's destructor body 0x00543560, then freed).
void DeleteNewPlayer(void* player)
{
    static_cast<JoinerRecord*>(player)->DestroyBody();
    MemSys::Hub::Pull()->Dealloc(player);
}

} // namespace

// 0x00536820
void Hub::RemoveAllNewPlayers()
{
    MemSys::ListNode* node = NewPlayerList(this)->next;
    if (IsHead(node))
        return;
    while (node) {
        void* player = node->data;
        MemSys::ListNode* next = IsHead(node->next) ? nullptr : node->next;
        if (m_host)
            m_host->SeverLink(Prop<NetLink::Conn*>(player, kNewPlayerConn));
        UnlinkListNode(node);
        if (player)
            DeleteNewPlayer(player);
        node = next;
    }
}

// 0x00536B10
void Hub::RemoveNewPlayer(void* player)
{
    MemSys::ListNode* node = NewPlayerList(this)->next;
    // (the original tests for the list's end before it tests for a null node)
    if (IsHead(node) || !node)
        return;
    while (node->data != player) {
        node = node->next;
        if (IsHead(node) || !node)
            return;
    }
    NetLink::Conn* conn = Prop<NetLink::Conn*>(player, kNewPlayerConn);
    if (m_host)
        m_host->SeverLink(conn);
    else if (conn)
        m_clientApp->SeverLink(conn);
    UnlinkListNode(node);
    DeleteNewPlayer(player);
}

// 0x00537010
void* Hub::StepPlayerWalk(void* search, bool observers)
{
    auto* s = static_cast<PlayerSearch*>(search);
    auto* node = NextNode(static_cast<MemSys::ListNode*>(s->m_listLink));
    s->m_listLink = node;
    uint8_t* player = DataOf(node);
    if (!observers && player && IsObserver(player))
        return StepPlayerWalk(search, false);
    return player;
}

// 0x005372C0
void* Hub::BeginPlayerWalk(void* search, bool observers)
{
    auto* s = static_cast<PlayerSearch*>(search);
    MemSys::ListNode* head = PlayerList(this);
    s->m_listLink = head;
    MemSys::ListNode* node = NextNode(head);
    s->m_listLink = node;
    uint8_t* player = DataOf(node);
    if (!observers && player && IsObserver(player))
        return StepPlayerWalk(search, false);
    return player;
}

// 0x00537070
void* Hub::BeginJoinerWalk(void* search)
{
    auto* s = static_cast<NewPlayerSearch*>(search);
    MemSys::ListNode* head = NewPlayerList(this);
    s->m_listLink = head;
    MemSys::ListNode* node = NextNode(head);
    s->m_listLink = node;
    return DataOf(node);
}

// (no address: the lists' generic next, folded at 0x004573D0 - see the header)
void* Hub::StepJoinerWalk(void* search)
{
    auto* s = static_cast<NewPlayerSearch*>(search);
    MemSys::ListNode* next = static_cast<MemSys::ListNode*>(s->m_listLink)->next;
    s->m_listLink = next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
    return s->m_listLink ? next->data : nullptr;
}

// 0x005370A0
void* Hub::GetNewPlayerInfoByObjectId(uint16_t id)
{
    MemSys::ListNode* node = NextNode(NewPlayerList(this));
    for (uint8_t* player = DataOf(node); player; player = DataOf(node)) {
        if (static_cast<int>(Prop<int8_t>(player, kNewPlayerObjectId)) == static_cast<int>(id))
            return player;
        node = NextNode(node);
    }
    return nullptr;
}

// 0x00537140
void Hub::SetReadyState(bool ready)
{
    if (static_cast<uint8_t>(ready) == *reinterpret_cast<uint8_t*>(&m_readyToPlay))
        return;
    m_readyToPlay = ready;
    uint8_t* skate = Detail::Skate();
    unsigned int count = TallyNodes(Prop<MemSys::ListNode*>(skate, kSkaterListFirst));
    for (unsigned int i = 0; i < count; ++i) {
        auto* skater = Call<void***>(CodeAddress(&GameModules::Skate::GetRiderAt), skate, i);
        if (skater) // (the skater's slot 12 when ready, 11 when not)
            reinterpret_cast<void(__thiscall*)(void*)>((*skater)[ready ? 12 : 11])(skater);
    }
}

// 0x00537320
void* Hub::GetOwnPlayer() { return FindPlayer(this, &PlayerRecord::IsOwnPlayer); }

// 0x005373D0
void* Hub::GetHostPlayer() { return FindPlayer(this, &PlayerRecord::IsHostPlayer); }

// 0x00537480
void* Hub::GetHillRuler() { return FindPlayer(this, &PlayerRecord::HoldsCrown); }

// 0x00537530
int Hub::CountOtherPlayers()
{
    PlayerSearch search;
    int count = 0;
    for (void* player = BeginPlayerWalk(&search, false); player; player = StepPlayerWalk(&search, false)) {
        if (!static_cast<PlayerRecord*>(player)->HasFinishedJoining()) // (flags 0x21)
            ++count;
    }
    return count;
}

// 0x00537620
void Hub::ClearRoster()
{
    for (MemSys::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        Prop<uint32_t>(player, kPlayerFlags) &= ~0x00020100u;
        Prop<uint32_t>(player, 0x20) = 0;
        Prop<uint32_t>(player, 0x1C) = 0;
        Prop<uint32_t>(player, 0x34) = 0;
        auto* skater = reinterpret_cast<GameObjects::CGameEntity*>(PlayerSkater(player));
        if (skater) {
            auto* score = skater->FetchModule(kSkaterScore); // (not checked for null, as in the original)
            if (Prop<void*>(score, 0x18))
                Call(CodeAddress(&GameModules::PointTally::Reset), Prop<void*>(score, 0x18));
        }
    }
}

// 0x005376F0
void Hub::BuildNetNotice(bool broadcast, uint32_t text, const char* string1, const char* string2, void*,
                                    const char* properties, bool important, int time)
{
    if (broadcast) {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        PanelMessage message;
        message.string1[0] = 0;
        message.string2[0] = 0;
        message.text = text;
        message.time = time;
        message.properties = Checksums::HashFromText(properties);
        if (string1)
            std::strcpy(message.string1, string1);
        if (string2)
            std::strcpy(message.string2, string2);
        desc.m_content = &message;
        desc.m_byteCount = sizeof(PanelMessage);
        desc.m_ident = kMsgPanelMessage;
        if (important) {
            desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
            desc.m_groupId = kGroupGame;
        }
        for (MemSys::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node))
            m_host->PutOnOutbox(PlayerHandle(player), &desc);
        return;
    }
    Scripts::CStruct* params = NewParams();
    const char* format = Scripts::FetchText(text);
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
    params->AppendWord(kText, Scripts::QBVAL_STRING, shown);
    params->AddComponentOfType(kId, Scripts::QBVAL_NAME, kNetPanelMsg);
    params->PutInt(kMsgTime, time);
    Run(kCreateNetPanelMessage, params);
    DeleteParams(params);
}

// 0x00537A10
void Hub::QueueScript(int node, uint32_t objectId, uint32_t script)
{
    MemSys::ListNode* head = QueuedScriptList(this);
    for (MemSys::ListNode* n = NextNode(head); auto* queued = reinterpret_cast<QueuedScript*>(DataOf(n)); n = NextNode(n)) {
        if (queued->m_runScript == script)
            return;
    }
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_networkHeap);
    auto* queued = static_cast<QueuedScript*>(MemSys::AllocZeroed(sizeof(QueuedScript)));
    if (queued) { // (a null one is written to anyway, as in the original)
        queued->data = queued;
        queued->priority = 0;
        queued->next = queued;
        queued->prev = queued;
        queued->vtable = kQueuedScriptVtable;
    }
    queued->m_listLink = node;
    queued->m_entityKey = objectId;
    queued->m_runScript = script;
    // Onto the end of the list.
    queued->prev = head->prev;
    queued->next = head;
    head->prev->next = queued;
    head->prev = queued;
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00537B30
void Hub::ClearQueuedScripts()
{
    MemSys::ListNode* node = NextNode(QueuedScriptList(this));
    for (uint8_t* queued = DataOf(node); queued;) {
        node = NextNode(node);
        uint8_t* next = DataOf(node);
        UnlinkListNode(reinterpret_cast<MemSys::ListNode*>(queued));
        queued = next;
    }
}

// 0x00537D90
void Hub::LaunchQueuedScripts()
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    auto* sound = SoundFx::g_sfxManager;
    uint8_t* skate = Detail::Skate();
    memory->EnterHeapScope(memory->m_bottomUpHeap);
    Call(CodeAddress(&GameModules::Skate::BeginQueuedScripts), skate);
    float volume = SoundFx::FetchMasterLoudness();
    sound->ChangeMasterLoudness(0.0f);
    Run(kPreRunQueuedScripts);
    MemSys::ListNode* node = NextNode(QueuedScriptList(this));
    for (auto* queued = reinterpret_cast<QueuedScript*>(DataOf(node)); queued;) {
        node = NextNode(node);
        auto* next = reinterpret_cast<QueuedScript*>(DataOf(node));
        Scripts::CScript* script =
            Scripts::SpawnScript(queued->m_runScript, nullptr, 0, nullptr, queued->m_listLink, 0, false, false, false, false);
        GameObjects::CEntity* object = reinterpret_cast<GameObjects::CObjectHubBase*&>(GameObjects::g_compositeObjectManager)->LookUpEntity(queued->m_entityKey);
        script->m_entity.AssignValue(object);
        while (script->TickFrame() != Scripts::SCRIPT_OUTCOME_FINISHED) {
        }
        Scripts::KillSpawnedScript(script);
        UnlinkListNode(queued);
        queued = next;
    }
    Run(kPostRunQueuedScripts);
    sound->ChangeMasterLoudness(volume);
    Call(CodeAddress(&GameModules::Skate::EndQueuedScripts), skate);
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00537F40
void Hub::ToggleProSet(uint8_t bit, uint32_t param)
{
    NetLink::NetMessageInfo desc;
    desc.MakeNew();
    struct {
        uint32_t param;
        uint8_t bit;
        uint8_t pad[3]; // (the original's stack)
    } message = {param, bit, {0, 0, 0}};
    m_proSetFlags ^= 1u << (bit & 31);
    desc.m_byteCount = sizeof(message);
    desc.m_groupId = kGroupGame;
    desc.m_content = &message;
    desc.m_ident = kMsgToggleProSet;
    desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    NetLink::AppHost* server = m_host;
    for (MemSys::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if (!IsOwnPlayer(player))
            server->PutOnOutbox(PlayerHandle(player), &desc);
    }
}

// 0x00539880
int Hub::CountTeamPlayers(int team)
{
    PlayerSearch search;
    int count = 0;
    for (auto* player = static_cast<uint8_t*>(BeginPlayerWalk(&search, false)); player;
         player = static_cast<uint8_t*>(StepPlayerWalk(&search, false))) {
        if (PlayerTeam(player) == team)
            ++count;
    }
    for (MemSys::ListNode* node = NextNode(NewPlayerList(this)); uint8_t* info = DataOf(node); node = NextNode(node)) {
        if (WillPlay(info) && Prop<int>(info, kNewPlayerTeam) == team)
            ++count;
    }
    return count;
}

// 0x00539B00
int Hub::CountPlayers()
{
    // The players' skater ids (zeroed first: the original's check below reads the byte after the last one noted).
    uint8_t ids[12] = {};
    int noted = 0;
    int count = 0;
    for (MemSys::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if (!IsObserver(player) || (Prop<uint8_t>(player, kPlayerFlags) & 8)) {
            if (GameObjects::CEntity* skater = PlayerSkater(player))
                ids[noted++] = static_cast<uint8_t>(SkaterId(skater));
            ++count;
        }
    }
    for (MemSys::ListNode* node = NextNode(NewPlayerList(this)); uint8_t* info = DataOf(node); node = NextNode(node)) {
        if (!WillPlay(info))
            continue;
        bool listed = false;
        for (int i = 0; i < noted && !listed; ++i)
            listed = Prop<uint8_t>(info, kNewPlayerObjectId) == ids[noted]; // (ids[noted], as in the original)
        if (!listed)
            ++count;
    }
    return count;
}

// 0x00539C70
int Hub::CountObservers()
{
    int count = 0;
    for (MemSys::ListNode* node = NextNode(PlayerList(this)); uint8_t* player = DataOf(node); node = NextNode(node)) {
        if (IsObserver(player) && !AsPlayer(player)->IsPendingObserver() && (!IsOwnPlayer(player) || m_hostMode == 0))
            ++count;
    }
    for (MemSys::ListNode* node = NextNode(NewPlayerList(this)); uint8_t* info = DataOf(node); node = NextNode(node)) {
        uint32_t flags = Prop<uint32_t>(info, kNewPlayerFlags);
        if ((flags & 4) && !(flags & 8))
            ++count;
    }
    return count;
}

// 0x005399F0
void Hub::DisconnectClients()
{
    NetLink::Hub* net = NetLink::Hub::Sole();
    for (int i = 0; i < 2; ++i) {
        if (!ClientSlot(i))
            continue;
        if (i == 0 && !m_clientApp->IsHostLocal())
            m_clientAddNewPlayersTask->DropOut();
        net->ReleaseApp(ClientSlot(i));
        ClientSlot(i) = nullptr;
    }
    if (m_observerInputHandler) {
        DeleteVirtual(m_observerInputHandler);
        m_observerInputHandler = nullptr;
    }
    g_textEntry = 0;
    if (g_queryReporting) {
        GameSpy::Qr2Shutdown(nullptr);
        g_queryReporting = false;
    }
    GameSpy::CdKeyDisconnectAll(1003);
    Prop<TaskList::SchedulerTask*>(this, 0xD4)->DropOut();
    Prop<TaskList::SchedulerTask*>(this, 0xC8)->DropOut();
    Prop<TaskList::SchedulerTask*>(this, 0xEC)->DropOut();
    ClearQueuedScripts();
    if (m_readyToPlay) {
        m_readyToPlay = false;
        uint8_t* skate = Detail::Skate();
        unsigned int count = TallyNodes(Prop<MemSys::ListNode*>(skate, kSkaterListFirst));
        for (unsigned int i = 0; i < count; ++i) {
            auto* skater = Call<void***>(CodeAddress(&GameModules::Skate::GetRiderAt), skate, i);
            if (skater)
                reinterpret_cast<void(__thiscall*)(void*)>((*skater)[11])(skater);
        }
    }
}

// 0x00539DA0
void Hub::SendTagTexturesTo(void* target)
{
    auto* player = static_cast<uint8_t*>(target);
    PlayerSearch search;
    if (NetLink::Hub::Sole()->GetNetLinkKind() == NetLink::Hub::kConnectionModem || IsOwnPlayer(player))
        return;
    for (auto* other = static_cast<uint8_t*>(BeginPlayerWalk(&search, false)); other;
         other = static_cast<uint8_t*>(StepPlayerWalk(&search, false))) {
        void* texture = AsPlayer(other)->GetGraffitiTexture(); // its custom tag texture
        if (!texture || other == player || PlayerConn(player)->GetLinkSpeedClass() == 1 || !AsPlayer(other)->HasCustomTag())
            continue;
        // (the logos are read from the receiving player's profile, as in the original)
        void* profile = Prop<void*>(player, kPlayerProfileInfo);
        uint32_t logos = ProfileHasPart(profile, kHatLogo) ? 1 : 0;
        if (ProfileHasPart(profile, kFrontLogo))
            logos |= 2;
        if (ProfileHasPart(profile, kBackLogo))
            logos |= 4;
        if (ProfileHasPart(profile, kDeckGraphic))
            logos |= 8;
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        uint8_t skaterId = static_cast<uint8_t>(SkaterId(PlayerSkater(other)));
        desc.m_content = &skaterId;
        desc.m_ident = kMsgTagOwner;
        desc.m_byteCount = 1;
        desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
        desc.m_groupId = kGroupTag;
        m_host->PutOnOutbox(PlayerHandle(player), &desc);
        void* palette = TexturePalette(texture);
        m_host->SendBulkToConn(PlayerConn(player), kMsgTagPalette, TexturePaletteSize(texture), palette,
                                      kTagPaletteDesc, kGroupTag, false, true);
        void* pixels = TexturePixels(texture);
        m_host->SendBulkToConn(PlayerConn(player), kMsgTagTexture, TextureSize(texture), pixels, kTagTextureDesc,
                                      kGroupTag, false, true);
        desc.m_content = &logos;
        desc.m_ident = kMsgTagLogos;
        desc.m_byteCount = 4;
        desc.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
        desc.m_groupId = kGroupTag;
        m_host->PutOnOutbox(PlayerHandle(player), &desc);
    }
}

// 0x0053A040
bool Hub::PrepareLocalFace()
{
    if (NetLink::Hub::Sole()->GetNetLinkKind() == NetLink::Hub::kConnectionModem)
        return false;
    void* profile = Call<void*>(CodeAddress(static_cast<GameObjects::CRiderIdentity* (GameModules::Skate::*)(int)>(&GameModules::Skate::FetchAccount)), Detail::Skate(), 0);
    void* face = Prop<void*>(profile, 0x54); // its face texture (0x00576E60)
    if (!face || !Call<bool>(CodeAddress(&Gfx2D::CFaceSkin::IsSound), face))
        return false;
    MemSys::Hub* memory = MemSys::Hub::Pull();
    if (m_flagBits & 1) {
        auto* local = static_cast<uint8_t*>(GetOwnPlayer());
        memory->EnterHeapScope(memory->m_netMiscHeap);
        Prop<uint8_t*>(local, kPlayerFace) = static_cast<uint8_t*>(MemSys::Alloc(kFaceSize + 1));
        MemSys::Hub::Pull()->LeaveHeapScope();
        Prop<uint8_t*>(local, kPlayerFace)[0] = 0;
        Call(CodeAddress(&Gfx2D::CFaceSkin::WriteTo), face, Prop<uint8_t*>(local, kPlayerFace) + 1, kFaceSize);
    } else {
        NetLink::ConnSearch search;
        search.m_vtable = NetLink::kConnSearchVtable;
        search.m_listLink = nullptr;
        if (m_clientApp->BeginConnWalk(&search)->GetLinkSpeedClass() != 0)
            return false;
        memory->EnterHeapScope(memory->m_networkHeap);
        void* data = MemSys::Alloc(kFaceSize);
        MemSys::Hub::Pull()->LeaveHeapScope();
        Call(CodeAddress(&Gfx2D::CFaceSkin::WriteTo), face, data, kFaceSize);
        static_cast<NetLink::Guest*>(m_clientApp)->SendBulkToHost(kMsgFace, kFaceSize, data, kFaceDesc, kGroupFace,
                                                                    false, false);
        MemSys::Dealloc(data);
    }
    return true;
}

// 0x0053A1B0
void Hub::SendFacesTo(void* target)
{
    auto* player = static_cast<uint8_t*>(target);
    PlayerSearch search;
    if (NetLink::Hub::Sole()->GetNetLinkKind() == NetLink::Hub::kConnectionModem)
        return;
    for (auto* other = static_cast<uint8_t*>(BeginPlayerWalk(&search, false)); other;
         other = static_cast<uint8_t*>(StepPlayerWalk(&search, false))) {
        if (other == player || PlayerConn(player)->GetLinkSpeedClass() == 1)
            continue;
        if (!AsPlayer(other)->GetFaceTextureData() && !(IsOwnPlayer(other) && PrepareLocalFace()))
            continue;
        m_host->SendBulkToConn(PlayerConn(player), kMsgFace, kFaceSize + 1, AsPlayer(other)->GetFaceTextureData(),
                                      kFaceDesc, kGroupFace, false, true);
    }
}

// 0x0053A310
void Hub::AddJoiningPlayer(void* message, int timeout)
{
    auto* info = static_cast<uint8_t*>(message);
    MemSys::ListNode* head = NewPlayerList(this);
    for (MemSys::ListNode* node = NextNode(head); uint8_t* listed = DataOf(node); node = NextNode(node)) {
        if (Prop<uint8_t>(listed, kNewPlayerObjectId) == Prop<uint8_t>(info, kNewPlayerObjectId))
            return;
    }
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_networkHeap);
    void* recordMemory = MemSys::AllocZeroed(kNewPlayerSize);
    auto* record = reinterpret_cast<uint8_t*>(recordMemory ? static_cast<JoinerRecord*>(recordMemory)->MakeNew() : nullptr);
    std::strcpy(reinterpret_cast<char*>(record), reinterpret_cast<const char*>(info));
    Prop<uint32_t>(record, 0x14) = Prop<uint32_t>(info, 0x14);
    Prop<uint8_t>(record, kNewPlayerObjectId) = Prop<uint8_t>(info, kNewPlayerObjectId);
    for (uint32_t offset : {0x20u, 0x28u, 0x2Cu, 0x30u, 0x34u, 0x38u, 0x3Cu, 0x40u})
        Prop<uint32_t>(record, offset) = Prop<uint32_t>(info, offset);
    uint32_t flags = Prop<uint32_t>(record, kNewPlayerFlags);
    if ((flags & 0x10) && (m_flagBits & 1))
        Prop<int>(record, 0x24) = m_host->m_frameCount;
    if (!(flags & 4) || (flags & 8)) {
        // The appearance, through a buffer.
        if (!MemSys::g_managerReady)
            MemSys::EnsureManager();
        auto* buffer = static_cast<uint8_t*>(MemSys::Hub::Pull()->Carve(0x1000, 1, nullptr));
        Prop<GameObjects::CRiderIdentity*>(info, kNewPlayerProfile)->EmitIntoBuffer(buffer, 0x1000, 0);
        Prop<GameObjects::CRiderIdentity*>(record, kNewPlayerProfile)->DecodeFromBytes(buffer);
        MemSys::Hub::Pull()->Dealloc(buffer);
    }
    if (m_flagBits & 1) {
        flags = Prop<uint32_t>(record, kNewPlayerFlags);
        Prop<int>(record, 0x18) = m_host->m_frameCount + timeout;
        if (!(flags & 1)) {
            const char* name = reinterpret_cast<const char*>(info);
            if (flags & 4)
                BuildNetNotice(true, (flags & 8) ? kNetMessageJoinPending : kNetMessageObserving, name, nullptr,
                                      nullptr, nullptr, false, 2000);
            else
                BuildNetNotice(false, kNetMessageJoining, name, nullptr, nullptr, nullptr, false, 2000);
        }
    } else {
        Prop<int>(record, 0x18) = m_clientApp->m_frameCount + timeout;
    }
    // Its node, onto the end of the list.
    auto* node = static_cast<MemSys::ListNode*>(MemSys::AllocZeroed(sizeof(MemSys::ListNode)));
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
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x0053A580
void* Hub::NextCamPlayer()
{
    PlayerSearch search;
    auto canWatch = [](void* player) {
        return !static_cast<PlayerRecord*>(player)->IsScoutingLevel() && !static_cast<PlayerRecord*>(player)->IsEliminated();
    };
    if (m_camPlayer) {
        void* player = BeginPlayerWalk(&search, false);
        while (player && player != m_camPlayer)
            player = StepPlayerWalk(&search, false);
        if (player) {
            // The ones after it; past the end, from the start again.
            for (player = StepPlayerWalk(&search, false); player; player = StepPlayerWalk(&search, false)) {
                if (canWatch(player))
                    return player;
            }
            m_camPlayer = nullptr;
            return NextCamPlayer();
        }
    }
    for (void* player = BeginPlayerWalk(&search, false); player; player = StepPlayerWalk(&search, false)) {
        if (canWatch(player))
            return player;
    }
    return nullptr;
}

Hub* g_gameNetManager;

// 0x00539610 (a task's code: the manager's flag task): in capture the flag, each flag a player (no observers)
// carries (flags 0x7800) put on the carrier's head - the flag object of the team it belongs to (TRG_CTF_Red / Blue /
// Green / Yellow; none: the task stops for the frame) gets the head bone's matrix, turned 90 degrees about x and
// raised 12, then the skater's own matrix; its position the bone's offset transformed, plus the skater's position.
void __cdecl CarriedFlagsTask(void*)
{
    Hub* manager = g_gameNetManager;
    PlayerSearch search;
    if (GameType() != kNetCtf)
        return;
    for (auto* player = static_cast<uint8_t*>(manager->BeginPlayerWalk(&search, false)); player;
         player = static_cast<uint8_t*>(manager->StepPlayerWalk(&search, false))) {
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
        auto* flag = reinterpret_cast<uint8_t*>(GameObjects::FindObject(name));
        if (!flag)
            return;
        auto* skater = reinterpret_cast<GameObjects::CGameEntity*>(PlayerSkater(player));
        // (the component's +0x18, CBoneRigModule::m_boneRig, through the getter folded at 0x005480F0)
        auto* skeleton = reinterpret_cast<void*>(reinterpret_cast<FoldedGetters*>(skater->FetchModule(kSkeleton))->Word18());
        auto* matrix = reinterpret_cast<Maths::Mat4*>(flag + 0x6C);
        Call(CodeAddress(&Gfx2D::CRig::ReadJointMatrix), skeleton, kBoneHead, matrix); // the head bone's matrix
        Maths::Mat4 turn;
        D3DXMatrixMultiply(matrix, &Maths::MakePitchMatrix(turn, kQuarterTurn), matrix);
        float raise[4] = {0.0f, 12.0f, 0.0f, 0.0f};
        Call(CodeAddress(&Maths::Mat4::OffsetLocal), matrix, raise); // (moved along its own axes)
        float offset[4];
        std::memcpy(offset, flag + 0x9C, sizeof(offset)); // its translation row, taken out
        float identityRow[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        std::memcpy(flag + 0x9C, identityRow, sizeof(identityRow));
        auto* skaterMatrix = reinterpret_cast<Maths::Mat4*>(reinterpret_cast<uint8_t*>(skater) + 0x6C);
        D3DXMatrixMultiply(matrix, matrix, skaterMatrix);
        Maths::Vec4 moved;
        const Maths::Vec4& out = skaterMatrix->Xform(moved, *reinterpret_cast<const Maths::Vec4*>(offset));
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
    uint32_t& references = g_gameNetReferences;
    if (references > 1) {
        --references;
        return;
    }
    Hub* manager = g_gameNetManager;
    if (manager) {
        manager->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(manager);
    }
    g_gameNetManager = nullptr;
    references = 0;
}

} // namespace OnlinePlay
