// The network manager's player queries and the observer's camera (GameNet::Manager, 0x0053A910-0x0053AF90), and the
// host mode (0x0053C320). See gamenet_manager.h.

#include <cstdint>

#include "game/core/task.h"
#include "game/sys/timer.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/mlp.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint8_t kMsgObserveRequest = 0x4A; // (the server's HandleObserveRequest)
constexpr uint8_t kMsgFcfsNotice = 0x5F;     // (the client's HandleFcfsNotice)

constexpr uint32_t kSkaterCamObject = 0x967C138C;     // the camera object (by id)
constexpr uint32_t kSkaterCamera = 0x5E43A604;        // SkaterCamera
constexpr uint32_t kWalkCamera = 0xC7D37EC2;          // WalkCamera
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // SkaterPhysicsControl
constexpr uint32_t kFcfsPreference = 0x043EE978;      // the network preference naming the first-come notice's text

constexpr int kMaxObjectIds = 16;
constexpr uint32_t kNewPlayerObserver = 4; // NewPlayerInfo flags
constexpr uint32_t kNewPlayerPending = 8;

// PlayerInfo flags (+0x1D0).
constexpr uint32_t kPlayerFcfsCandidate = 0x21; // (0x00543380: local or 0x20)

Tsk::Stack* LogicTasks()
{
    return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(GameVariable<Mlp::Manager*>(0x00696D68)) + 4);
}

// The skater camera's / walk camera's calls not rewritten as members here.
void SetCameraSkater(Obj::CBaseComponent* camera, void* skater)
{
    GameFunction<void(__thiscall*)(void*, void*)>(0x004FA3C0)(camera, skater);
}
void SetWalkCameraSkater(Obj::CBaseComponent* camera, void* skater)
{
    GameFunction<void(__thiscall*)(void*, void*)>(0x00524C50)(camera, skater);
}
void ResetWalkCamera(Obj::CBaseComponent* camera) { GameFunction<void(__thiscall*)(void*)>(0x00524C30)(camera); }

} // namespace

// 0x0053A910
void Manager::ObservePlayer(uint8_t* player)
{
    auto* camObject = static_cast<Obj::CCompositeObject*>(
        reinterpret_cast<Obj::CBaseManager*>(GameVariable<void*>(0x006F0FB8))->GetObjectByID(kSkaterCamObject));
    Obj::CBaseComponent* skaterCamera = camObject->GetComponent(kSkaterCamera);
    Obj::CBaseComponent* walkCamera = camObject->GetComponent(kWalkCamera);
    if (!player || !PlayerSkater(player)) {
        SetCameraSkater(skaterCamera, nullptr);
        SetWalkCameraSkater(walkCamera, nullptr);
        Field<int>(walkCamera, 0x18) = 3; // (frames that jump straight to the target)
        m_camPlayer = nullptr;
        return;
    }
    auto* skater = reinterpret_cast<Obj::CCompositeObject*>(PlayerSkater(player));
    if (player != LocalPlayer(GameVariable<Manager*>(0x007CCE14))) {
        // Someone else's skater: the skater camera onto it.
        SetCameraSkater(skaterCamera, skater);
        SetWalkCameraSkater(walkCamera, nullptr);
        Field<int>(walkCamera, 0x18) = 3;
        skaterCamera->VirtualSuspend(false);
        walkCamera->VirtualSuspend(true);
        m_camPlayer = player;
        return;
    }
    // The local skater: the walk camera while it walks (its physics control's state says), else the skater camera.
    Obj::CBaseComponent* control = skater->GetComponent(kSkaterPhysicsControl);
    SetCameraSkater(skaterCamera, skater);
    SetWalkCameraSkater(walkCamera, skater);
    Field<int>(walkCamera, 0x18) = 3;
    uint8_t* state = Field<uint8_t*>(control, 0x2C);
    if (!Field<bool>(state, 0x118) && Field<int>(state, 0x1C) == 0) {
        skaterCamera->VirtualSuspend(false);
        walkCamera->VirtualSuspend(true);
    } else {
        skaterCamera->VirtualSuspend(true);
        walkCamera->VirtualSuspend(false);
        ResetWalkCamera(walkCamera);
    }
    m_camPlayer = player;
}

// 0x0053AA70
void Manager::SendObserveRequest()
{
    Net::MsgDesc desc;
    desc.Construct();
    if (IsObserver(LocalPlayer(this))) // (no local player: the original reads through null too)
        return;
    desc.m_id = kMsgObserveRequest;
    desc.m_queue = Net::MsgDesc::kQueueSequenced;
    desc.m_groupId = kGroupGame;
    SendToServer(m_client, &desc);
}

// 0x0053AAC0
void Manager::AssignFirstComeFirstServed()
{
    PlayerSearch search;
    Net::MsgDesc desc;
    desc.Construct();
    for (uint8_t* player = FirstPlayer(this, &search, false); player; player = NextPlayer(this, &search, false)) {
        if (IsLocalPlayer(player) || !(Field<uint32_t>(player, 0x1D0) & kPlayerFcfsCandidate))
            continue;
        Call(0x00543390, player); // (its flag 0x200: first come, first served)
        uint32_t text;
        reinterpret_cast<Prefs::Preferences*>(m_networkPreferences)
            ->GetPreference(kFcfsPreference)
            ->GetChecksum("Checksum", &text, 1);
        desc.m_data = &text;
        desc.m_length = sizeof(text);
        desc.m_id = kMsgFcfsNotice;
        desc.m_queue = Net::MsgDesc::kQueueSequenced;
        desc.m_groupId = kGroupGame;
        m_server->EnqueueMessage(PlayerHandle(player), &desc);
        return;
    }
}

// 0x0053AC40
int Manager::NextPlayerObjectId()
{
    for (int id = 0; id < kMaxObjectIds; ++id) {
        bool used = false;
        PlayerSearch search;
        for (uint8_t* player = FirstPlayer(this, &search, false); player && !used;
             player = NextPlayer(this, &search, false))
            used = static_cast<int>(SkaterId(PlayerSkater(player))) == id; // (a player without a skater: read at 0x14)
        NewPlayerSearch newSearch;
        for (uint8_t* info = FirstNewPlayer(this, &newSearch); info && !used; info = NextNewPlayer(this, &newSearch)) {
            uint32_t flags = Field<uint32_t>(info, 0x20);
            if ((flags & kNewPlayerObserver) && !(flags & kNewPlayerPending))
                continue; // (an observer joining takes no id)
            used = Field<int8_t>(info, 0x10) == id;
        }
        if (!used)
            return id;
    }
    return kMaxObjectIds;
}

// 0x0053AE10
uint8_t* Manager::PlayerByConnection(Net::Conn* conn)
{
    PlayerSearch search;
    for (uint8_t* player = FirstPlayer(this, &search, true); player; player = NextPlayer(this, &search, true))
        if (Field<Net::Conn*>(player, 0x18) == conn)
            return player;
    return nullptr;
}

// 0x0053AEA0
uint8_t* Manager::PlayerByObjectId(uint16_t id)
{
    PlayerSearch search;
    for (uint8_t* player = FirstPlayer(this, &search, false); player; player = NextPlayer(this, &search, false)) {
        Obj::CObject* skater = PlayerSkater(player);
        if (skater && SkaterId(skater) == id)
            return player;
    }
    return nullptr;
}

// 0x0053C320
void Manager::SetHostMode(int mode)
{
    if (mode == m_hostMode)
        return;
    m_hostMode = mode;
    if (mode == kHostModeAutoServe) {
        SendObserveRequest();
        GameVariable<uint32_t>(0x007CCDEC) = Tmr::GetTime(); // (the auto-serve task waits 2 s from here)
        LogicTasks()->AddTask(Field<Tsk::BaseTask*>(this, 0xF4));
    } else if (mode == kHostModeFcfs) {
        SendObserveRequest();
        AssignFirstComeFirstServed();
    }
}

} // namespace GameNet

REPLACE(0x0053A910, &GameNet::Manager::ObservePlayer);
REPLACE(0x0053AA70, &GameNet::Manager::SendObserveRequest);
REPLACE(0x0053AAC0, &GameNet::Manager::AssignFirstComeFirstServed);
REPLACE(0x0053AC40, &GameNet::Manager::NextPlayerObjectId);
REPLACE(0x0053AE10, &GameNet::Manager::PlayerByConnection);
REPLACE(0x0053AEA0, &GameNet::Manager::PlayerByObjectId);
REPLACE(0x0053C320, &GameNet::Manager::SetHostMode);
