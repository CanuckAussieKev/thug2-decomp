// The special item component: the items an object carries. See special_item.h.

#include "game/rider/special_item.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/objects/node_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/model.h"
#include "engine/core/code_address.h"
#include "game/rider/skitch.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kLockObj = 0x5CB5CB7E;

// Member functions.
constexpr uint32_t kCreateSpecialItem = 0x827C6BD1;
constexpr uint32_t kDestroySpecialItem = 0x742188FA;
constexpr uint32_t kDestroyAllSpecialItems = 0xA1BAA25E;
constexpr uint32_t kSpecialItemExists = 0xCE887492;

// Parameter and tag names.
constexpr uint32_t kIndex = 0x7F8C98FE;
constexpr uint32_t kBone = 0xCAB94088;
constexpr uint32_t kParams = 0x7031F10C;
constexpr uint32_t kId = 0xA1DC81F9;
constexpr uint32_t kOwner = 0x7A9597B3;
constexpr uint32_t kOnDestroyScript = 0x40764820;
constexpr uint32_t kObjectId = 0x40C698AF;

// Network messages to the server.
constexpr uint8_t kMsgCreateSpecialItem = 0x77;
constexpr uint8_t kMsgDestroySpecialItem = 0x78;

template <typename T>
T& Prop(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* memory = Scripts::SpawnParamBag();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

// The object marked dead (its vtable +0x18: a composite object tells its components first).
void FlagAsDead(CGameEntity* object) { reinterpret_cast<void(__thiscall*)(CGameEntity*)>(object->m_vtable[6])(object); }

// An object's model's data (its model component's +0x208), or null.
uint8_t* ModelData(CGameEntity* object)
{
    CObjectModule* model = object->FetchModule(kModel);
    return model ? Prop<uint8_t*>(model, 0x208) : nullptr;
}

// A message to the server, in a network game.
void SendToServer(uint8_t id, void* data)
{
    auto* manager = OnlinePlay::g_onlineHub;
    if (!manager->InNetGame())
        return;
    NetLink::NetMessageInfo msg;
    msg.MakeNew();
    auto* client = static_cast<NetLink::Guest*>(manager->FetchPatron(0));
    msg.m_content = data;
    msg.m_byteCount = 0xC;
    msg.m_ident = id;
    client->SendToHost(&msg);
}

} // namespace

// 0x0051BD80
CObjectModule* __cdecl CPickupItemHandler::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CPickupItemHandler));
    if (!memory)
        return nullptr;
    auto* items = static_cast<CPickupItemHandler*>(memory);
    items->CObjectModule::MakeNew();
    items->m_vtable = kVtable;
    items->m_kindTag = kType;
    items->m_items[0] = nullptr;
    items->m_items[1] = nullptr;
    return items;
}

// 0x0051BF20
void CPickupItemHandler::DestroyBody()
{
    m_vtable = kVtable;
    for (int slot = 0; slot < kItems; ++slot)
        DestroyItem(slot);
    CObjectModule::DestroyBody();
}

// 0x0051C130
CPickupItemHandler* CPickupItemHandler::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0051BE00
void CPickupItemHandler::DestroyItem(int slot)
{
    if (!m_items[slot])
        return;
    Scripts::CStruct* tags = NewParams();
    m_items[slot]->CopyTagsTo(tags);
    uint32_t script;
    if (tags->ReadHashValue(kOnDestroyScript, &script, 0))
        Scripts::LaunchScript(script, nullptr, m_items[slot], false, nullptr);
    DeleteParams(tags);
    FlagAsDead(m_items[slot]);
    m_items[slot] = nullptr;
}

// 0x0051BF00
void CPickupItemHandler::DestroyItems()
{
    for (int slot = 0; slot < kItems; ++slot)
        DestroyItem(slot);
}

// 0x0051BF80
CGameEntity* CPickupItemHandler::CreateItem(int slot, Scripts::CStruct* params)
{
    if (m_items[slot])
        DestroyItem(slot);
    uint32_t id;
    if (params->ReadHashValue(kId, &id, 0)) {
        id += m_entity->m_ident;
        params->PutNameKey(kId, id);
    }
    MemSys::PushMemProfile("Game Objects");
    CGameEntity* item = CreateGameObjectFromNode(params);
    MemSys::PopMemProfile();
    m_items[slot] = item;

    Scripts::CStruct* tags = NewParams();
    tags->PutNameKey(kOwner, m_entity->m_ident);
    uint32_t script;
    if (params->ReadHashValue(kOnDestroyScript, &script, 0))
        tags->PutNameKey(kOnDestroyScript, script);
    m_items[slot]->SetTagsFromStruct(tags);
    DeleteParams(tags);

    // In the object's colour.
    uint8_t* mine = ModelData(m_entity);
    uint8_t* its = ModelData(item);
    if (mine && its) {
        uint32_t colour[4] = {Prop<uint32_t>(mine, 0x1C), Prop<uint32_t>(mine, 0x20), Prop<uint32_t>(mine, 0x24),
                              Prop<uint32_t>(mine, 0x28)};
        reinterpret_cast<Engine3D::CRenderModel*>(its)->ChangeScale(reinterpret_cast<const Maths::Vec4*>(colour)); // (0x00492EE0: four words set)
    }
    return item;
}

// 0x0051C160
int CPickupItemHandler::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    switch (name) {
    case kCreateSpecialItem: {
        // (Without them the original's slot and params name are what its argument slots held: the member
        // function's name and the params pointer.)
        int slot = static_cast<int>(name);
        uint32_t bone = 0;
        uint32_t itemParams = reinterpret_cast<uint32_t>(params);
        params->FetchIntValue(kIndex, &slot, 1);
        params->ReadHashValue(kBone, &bone, 0);
        params->ReadHashValue(kParams, &itemParams, 1);
        CGameEntity* item = CreateItem(slot, Scripts::ReadSubStruct(itemParams));
        Scripts::CStruct* lock = NewParams();
        lock->AppendStructureIfAny(params);
        lock->PutNameKey(kObjectId, m_entity->m_ident);
        item->FetchModule(kLockObj)->ConfigureFromData(lock);
        DeleteParams(lock);
        struct {
            uint32_t params;
            uint32_t bone;
            uint8_t objectId;
            uint8_t slot;
            uint8_t pad[2];
        } created = {itemParams, bone, static_cast<uint8_t>(m_entity->m_ident), static_cast<uint8_t>(slot), {}};
        SendToServer(kMsgCreateSpecialItem, &created);
        return 1;
    }
    case kDestroySpecialItem: {
        int slot = static_cast<int>(reinterpret_cast<uint32_t>(params)); // (as above)
        params->FetchIntValue(kIndex, &slot, 1);
        DestroyItem(slot);
        struct {
            uint32_t unused[2]; // (not set by the original)
            uint8_t objectId;
            uint8_t slot;
            uint8_t pad[2];
        } destroyed = {{}, static_cast<uint8_t>(m_entity->m_ident), static_cast<uint8_t>(slot), {}};
        SendToServer(kMsgDestroySpecialItem, &destroyed);
        return 1;
    }
    case kDestroyAllSpecialItems:
        DestroyItems();
        return 1;
    case kSpecialItemExists: {
        int slot;
        if (params->FetchIntValue(kIndex, &slot, 0))
            return m_items[slot] != nullptr;
        for (CGameEntity* item : m_items)
            if (item)
                return 1;
        return 0;
    }
    default:
        return 2;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CPickupItemHandler::Teardown,
         &GameObjects::ComponentWithoutUpdate::TickFrame,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CPickupItemHandler::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
