// The skater's flip-and-rotate component (Obj::CSkaterFlipAndRotateComponent): see flip_and_rotate.h.

#include "game/skater/flip_and_rotate.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064BE20);
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
T& Field(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// The network manager (0x007CCE14) and its players (not rewritten yet): the player with an object id (0x0053AEA0),
// whether a player is this machine's (0x00543300), a player's skater's index (0x005434B0) and the client app of an
// index (0x00532EA0).
void* NetManager() { return GameVariable<void*>(0x007CCE14); }
void* PlayerByObjectId(uint32_t id)
{
    return GameFunction<void*(__thiscall*)(void*, uint32_t)>(0x0053AEA0)(NetManager(), id);
}
bool IsLocalPlayer(void* player) { return GameFunction<bool(__thiscall*)(void*)>(0x00543300)(player); }
int PlayerSkaterIndex(void* player) { return GameFunction<int(__thiscall*)(void*)>(0x005434B0)(player); }
Net::Client* ClientApp(int index)
{
    return GameFunction<Net::Client*(__thiscall*)(void*, int)>(0x00532EA0)(NetManager(), index);
}
// A client app's +0x08 (passed to the animation blender and to SetBoardRotated, which doesn't use it).
uint32_t AppValue8(Net::Client* app) { return Field<uint32_t>(app, 0x08); }

// The core physics component's (not rewritten yet): the skater turned around (0x004FE120) and whether the flip is
// needed (0x004FC440).
void TurnAround(uint8_t* corePhysics) { GameFunction<void(__thiscall*)(uint8_t*)>(0x004FE120)(corePhysics); }
bool FlipNeeded(uint8_t* corePhysics) { return GameFunction<bool(__thiscall*)(uint8_t*)>(0x004FC440)(corePhysics); }

// The animation blender's 0x00419C10: its animations mirrored or not (`flip`: the state component's byte as it is).
void FlipAnims(CBaseComponent* blender, uint32_t objectId, uint32_t flip, uint32_t a, int b)
{
    GameFunction<void(__thiscall*)(CBaseComponent*, uint32_t, uint32_t, uint32_t, int)>(0x00419C10)(blender, objectId,
                                                                                                   flip, a, b);
}

// The skater's index (+0x2BC of the skater object).
int SkaterIndex(CCompositeObject* skater) { return Field<int>(skater, 0x2BC); }

} // namespace

// 0x00511470
CBaseComponent* __cdecl CSkaterFlipAndRotateComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterFlipAndRotateComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterFlipAndRotateComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_animBlender = nullptr;
    component->m_model = nullptr;
    component->m_boardRotated = false;
    component->m_vtable = kVtable;
    component->m_type = kSkaterFlipAndRotate;
    return component;
}

// 0x00511300
void CSkaterFlipAndRotateComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00511320
CSkaterFlipAndRotateComponent* CSkaterFlipAndRotateComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00511350
void CSkaterFlipAndRotateComponent::Finalize()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCorePhysics));
    m_model = reinterpret_cast<uint8_t*>(m_object->GetComponent(kModel));
    m_animBlender = m_object->GetComponent(kAnimBlender);
}

// 0x00511390
bool CSkaterFlipAndRotateComponent::SetBoardRotated(uint32_t objectId, bool rotated, uint32_t /*unused*/, bool send)
{
    if (send) {
        void* player = PlayerByObjectId(objectId);
        if (player && IsLocalPlayer(player)) {
            Net::MsgDesc message;
            message.Construct();
            Net::Client* app = ClientApp(PlayerSkaterIndex(player));
            uint8_t data[2] = {static_cast<uint8_t>(objectId), static_cast<uint8_t>(rotated)};
            message.m_id = kMsgBoardRotated;
            message.m_length = 2;
            message.m_data = data;
            app->EnqueueMessageToServer(&message);
        }
    }
    bool was = m_boardRotated;
    m_boardRotated = rotated;
    return was;
}

// 0x00511310
void CSkaterFlipAndRotateComponent::ClearAfterFlags()
{
    m_flipAfter = false;
    m_rotateAfter = false;
}

// 0x00511420
void CSkaterFlipAndRotateComponent::ApplyFlipToAnims()
{
    Net::Client* app = ClientApp(SkaterIndex(m_object));
    uint8_t* state = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterState));
    FlipAnims(m_animBlender, m_object->m_id, Field<uint8_t>(state, 0x28), AppValue8(app), 1);
}

// 0x005114F0
void CSkaterFlipAndRotateComponent::Flip()
{
    uint8_t* state = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterState));
    uint8_t& flipped = Field<uint8_t>(state, 0x28);
    uint8_t flip = flipped == 0 ? 1 : 0;
    if (flipped != flip) { // (always)
        flipped = flip;
        Field<int>(state, 0x2C) = Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60);
    }
    ApplyFlipToAnims();
}

// 0x00511570
void CSkaterFlipAndRotateComponent::DoAfterAnim()
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
        Net::Client* app = ClientApp(PlayerSkaterIndex(PlayerByObjectId(m_object->m_id)));
        SetBoardRotated(m_object->m_id, !m_boardRotated, AppValue8(app), true);
        m_boardRotateAfter = false;
    }
    Field<uint8_t>(m_model, 0x1F4) = 0;
}

// 0x00511600
int CSkaterFlipAndRotateComponent::CallMemberFunction(uint32_t name, Script::CStruct* params,
                                                      Script::CScript* /*script*/)
{
    switch (name) {
    case kBoardIsRotated:
        return m_boardRotated ? 1 : 0;
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
        Net::Client* app = ClientApp(PlayerSkaterIndex(PlayerByObjectId(m_object->m_id)));
        bool rotated = params->ContainsFlag(kNormal) ? false : !m_boardRotated;
        SetBoardRotated(m_object->m_id, rotated, AppValue8(app), true);
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace Obj

REPLACE(0x00511470, Obj::CSkaterFlipAndRotateComponent::Create);
REPLACE(0x00511300, &Obj::CSkaterFlipAndRotateComponent::DestroyBody);
REPLACE(0x00511320, &Obj::CSkaterFlipAndRotateComponent::Destroy);
REPLACE(0x00511350, &Obj::CSkaterFlipAndRotateComponent::Finalize);
REPLACE(0x00511390, &Obj::CSkaterFlipAndRotateComponent::SetBoardRotated);
REPLACE(0x00511310, &Obj::CSkaterFlipAndRotateComponent::ClearAfterFlags);
REPLACE(0x00511420, &Obj::CSkaterFlipAndRotateComponent::ApplyFlipToAnims);
REPLACE(0x005114F0, &Obj::CSkaterFlipAndRotateComponent::Flip);
REPLACE(0x00511570, &Obj::CSkaterFlipAndRotateComponent::DoAfterAnim);
REPLACE(0x00511600, &Obj::CSkaterFlipAndRotateComponent::CallMemberFunction);
