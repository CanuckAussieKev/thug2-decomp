// The skater's flip-and-rotate component (GameObjects::CRiderSpinAndFlip): see flip_and_rotate.h.

#include "game/skater/flip_and_rotate.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/anim_blender.h"
#include "game/script/param_struct.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/game_clock.h"
#include "game/skater/core_physics.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kModel = 0x286A8D26, kAnimBlender = 0x7149B753,
                   kSkaterState = 0x43686585;

// Script member functions
constexpr uint32_t kBoardIsRotated = 0x79EE5CCF, kBoardRotateAfter = 0x63E93F69, kPlayAnim = 0x0B1E7291,
                   kRotateNoFlip = 0x587FBDD7, kFlip = 0x65011BAA, kFlipIfNeeded = 0x72289A28 /* (name unknown) */,
                   kFlipAndRotate = 0xD9DE0C65, kIsFlipAfterSet = 0xA6243DAA, kRotateAfter = 0xACAC1BAD,
                   kFlipAfter = 0xEBA11B20, kBoardRotate = 0xE0F3A644;
constexpr uint32_t kNormal = 0xDE7A971B;

constexpr uint8_t kMsgBoardRotated = 0x2A;

template <typename T>
T& Prop(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The network manager (0x007CCE14) and its players: the player with an object id, whether a player is this
// machine's, a player's skater's index (its +0x2BC) and the client app of an index.
OnlinePlay::Hub* NetSessionSlot() { return OnlinePlay::g_onlineHub; }
OnlinePlay::PlayerRecord* FindPlayerSlotByObj(uint32_t id)
{
    return reinterpret_cast<OnlinePlay::PlayerRecord*>(NetSessionSlot()->FindPlayerSlotByObj(static_cast<uint16_t>(id)));
}
bool IsOwnPlayer(OnlinePlay::PlayerRecord* player) { return player->IsOwnPlayer() != 0; }
int PlayerSkaterIndex(OnlinePlay::PlayerRecord* player) { return static_cast<int>(player->GetSkaterValue2BC()); }
NetLink::Guest* ClientApp(int index) { return static_cast<NetLink::Guest*>(NetSessionSlot()->FetchPatron(index)); }
// A client app's +0x08 (passed to the animation blender and to ApplyBoardTurned, which doesn't use it).
uint32_t AppValue8(NetLink::Guest* app) { return Prop<uint32_t>(app, 0x08); }

// The core physics component's (not rewritten yet): the skater turned around (0x004FE120) and whether the flip is
// needed (0x004FC440).
void TurnAround(uint8_t* corePhysics) { reinterpret_cast<Skater::SkaterPhysicsModule*>(corePhysics)->TurnAround(); }
bool FlipNeeded(uint8_t* corePhysics) { return (reinterpret_cast<Skater::SkaterPhysicsModule*>(corePhysics)->IsSidesSwapped() & 0xFF) != 0; }

// The animation blender's 0x00419C10: its animations mirrored or not (`flip`: the state component's byte as it is).
void FlipAnims(CObjectModule* blender, uint32_t objectId, uint32_t flip, uint32_t a, int b)
{
    static_cast<CAnimBlenderComponent*>(blender)->AnyChannel(objectId, flip, a, static_cast<uint32_t>(b));
}

// The skater's index (+0x2BC of the skater object).
int SkaterIndex(CGameEntity* skater) { return Prop<int>(skater, 0x2BC); }

} // namespace

// 0x00511470
CObjectModule* __cdecl CRiderSpinAndFlip::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderSpinAndFlip));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderSpinAndFlip*>(memory);
    component->CObjectModule::MakeNew();
    component->m_animBlender = nullptr;
    component->m_meshSet = nullptr;
    component->m_deckTurned = false;
    component->m_vtable = kVtable;
    component->m_kindTag = kSkaterFlipAndRotate;
    return component;
}

// 0x00511300
void CRiderSpinAndFlip::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00511320
CRiderSpinAndFlip* CRiderSpinAndFlip::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00511350
void CRiderSpinAndFlip::Seal()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterCorePhysics));
    m_meshSet = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kModel));
    m_animBlender = m_entity->FetchModule(kAnimBlender);
}

// 0x00511390
bool CRiderSpinAndFlip::ApplyBoardTurned(uint32_t objectId, bool rotated, uint32_t /*unused*/, bool send)
{
    if (send) {
        OnlinePlay::PlayerRecord* player = FindPlayerSlotByObj(objectId);
        if (player && IsOwnPlayer(player)) {
            NetLink::NetMessageInfo message;
            message.MakeNew();
            NetLink::Guest* app = ClientApp(PlayerSkaterIndex(player));
            uint8_t data[2] = {static_cast<uint8_t>(objectId), static_cast<uint8_t>(rotated)};
            message.m_ident = kMsgBoardRotated;
            message.m_byteCount = 2;
            message.m_content = data;
            app->SendToHost(&message);
        }
    }
    bool was = m_deckTurned;
    m_deckTurned = rotated;
    return was;
}

// 0x00511310
void CRiderSpinAndFlip::ClearAfterFlags()
{
    m_flipAfter = false;
    m_rotateAfter = false;
}

// 0x00511420
void CRiderSpinAndFlip::ApplyFlipToAnims()
{
    NetLink::Guest* app = ClientApp(SkaterIndex(m_entity));
    uint8_t* state = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterState));
    FlipAnims(m_animBlender, m_entity->m_ident, Prop<uint8_t>(state, 0x28), AppValue8(app), 1);
}

// 0x005114F0
void CRiderSpinAndFlip::Flip()
{
    uint8_t* state = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterState));
    uint8_t& flipped = Prop<uint8_t>(state, 0x28);
    uint8_t flip = flipped == 0 ? 1 : 0;
    if (flipped != flip) { // (always)
        flipped = flip;
        Prop<int>(state, 0x2C) = FrameClock::GameClockElapsed() + g_clockOffset;
    }
    // NOT ORIGINAL - THAW's BMX ([features] thaw_bmx, on the bike): THAW's Flip turns the flag only - the animations
    // take it when they're played (game/skater/thaw_bmx.h FlipFlagOnly / PlayFlip). Original behaviour: every running
    // animation mirrored at once.
    if (Skater::ThawBmx::FlipFlagOnly(m_corePhysics))
        return;
    ApplyFlipToAnims();
}

// 0x00511570
void CRiderSpinAndFlip::DoAfterAnim()
{
    if (m_flipAfter) {
        Flip();
        m_flipAfter = false;
    }
    if (m_rotateAfter) {
        TurnAround(m_corePhysics);
        m_rotateAfter = false;
    }
    if (m_boardRotateAfter) {
        // (the player isn't checked for null here)
        NetLink::Guest* app = ClientApp(PlayerSkaterIndex(FindPlayerSlotByObj(m_entity->m_ident)));
        ApplyBoardTurned(m_entity->m_ident, !m_deckTurned, AppValue8(app), true);
        m_boardRotateAfter = false;
    }
    Prop<uint8_t>(m_meshSet, 0x1F4) = 0;
}

// 0x00511600
int CRiderSpinAndFlip::InvokeMethodByName(uint32_t name, Scripts::CStruct* params,
                                                      Scripts::CScript* /*script*/)
{
    switch (name) {
    case kBoardIsRotated:
        return m_deckTurned ? 1 : 0;
    case kBoardRotateAfter:
        m_boardRotateAfter = true;
        return 1;
    case kPlayAnim:
        DoAfterAnim();
        return 2;
    case kRotateNoFlip:
        TurnAround(m_corePhysics);
        return 1;
    case kFlip:
        Flip();
        return 1;
    case kFlipIfNeeded:
        if (FlipNeeded(m_corePhysics))
            Flip();
        return 1;
    case kFlipAndRotate:
        TurnAround(m_corePhysics);
        Flip();
        return 1;
    case kIsFlipAfterSet:
        return m_flipAfter ? 1 : 0;
    case kRotateAfter:
        m_rotateAfter = true;
        return 1;
    case kFlipAfter:
        m_flipAfter = true;
        return 1;
    case kBoardRotate: {
        // (the player isn't checked for null here)
        NetLink::Guest* app = ClientApp(PlayerSkaterIndex(FindPlayerSlotByObj(m_entity->m_ident)));
        bool rotated = params->HasBareFlag(kNormal) ? false : !m_deckTurned;
        ApplyBoardTurned(m_entity->m_ident, rotated, AppValue8(app), true);
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderSpinAndFlip::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         Stubs::IgnoreArgument,
         &GameObjects::CRiderSpinAndFlip::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CRiderSpinAndFlip::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
