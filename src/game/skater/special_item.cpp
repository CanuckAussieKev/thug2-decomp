// The special item component: the items an object carries. See special_item.h.

#include "game/skater/special_item.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/obj/node_objects.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/gfx/model.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064C250);

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
constexpr uint32_t kDeathScript = 0x40764820;
constexpr uint32_t kObjectId = 0x40C698AF;

// Network messages to the server.
constexpr uint8_t kMsgCreateSpecialItem = 0x77;
constexpr uint8_t kMsgDestroySpecialItem = 0x78;

template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

Script::CStruct* NewParams()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

// The object marked dead (its vtable +0x18: a composite object tells its components first).
void MarkAsDead(CCompositeObject* object) { reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(object->m_vtable[6])(object); }

// An object's model's data (its model component's +0x208), or null.
uint8_t* ModelData(CCompositeObject* object)
{
    CBaseComponent* model = object->GetComponent(kModel);
    return model ? Field<uint8_t*>(model, 0x208) : nullptr;
}

// A message to the server, in a network game.
void SendToServer(uint8_t id, void* data)
{
    auto* manager = GameVariable<GameNet::Manager*>(0x007CCE14);
    if (!manager->InNetGame())
        return;
    Net::MsgDesc msg;
    msg.Construct();
    auto* client = static_cast<Net::Client*>(manager->GetClient(0));
    msg.m_data = data;
    msg.m_length = 0xC;
    msg.m_id = id;
    client->EnqueueMessageToServer(&msg);
}

} // namespace

// 0x0051BD80
CBaseComponent* __cdecl CSpecialItemComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSpecialItemComponent));
    if (!memory)
        return nullptr;
    auto* items = static_cast<CSpecialItemComponent*>(memory);
    items->CBaseComponent::Construct();
    items->m_vtable = kVtable;
    items->m_type = kType;
    items->m_items[0] = nullptr;
    items->m_items[1] = nullptr;
    return items;
}

// 0x0051BF20
void CSpecialItemComponent::DestroyBody()
{
    m_vtable = kVtable;
    for (int slot = 0; slot < kItems; ++slot)
        DestroyItem(slot);
    CBaseComponent::DestroyBody();
}

// 0x0051C130
CSpecialItemComponent* CSpecialItemComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0051BE00
void CSpecialItemComponent::DestroyItem(int slot)
{
    if (!m_items[slot])
        return;
    Script::CStruct* tags = NewParams();
    m_items[slot]->CopyTagsTo(tags);
    uint32_t script;
    if (tags->GetChecksum(kDeathScript, &script, 0))
        Script::RunScript(script, nullptr, m_items[slot], false, nullptr);
    DeleteParams(tags);
    MarkAsDead(m_items[slot]);
    m_items[slot] = nullptr;
}

// 0x0051BF00
void CSpecialItemComponent::DestroyItems()
{
    for (int slot = 0; slot < kItems; ++slot)
        DestroyItem(slot);
}

// 0x0051BF80
CCompositeObject* CSpecialItemComponent::CreateItem(int slot, Script::CStruct* params)
{
    if (m_items[slot])
        DestroyItem(slot);
    uint32_t id;
    if (params->GetChecksum(kId, &id, 0)) {
        id += m_object->m_id;
        params->AddChecksum(kId, id);
    }
    Mem::PushMemProfile("Game Objects");
    CCompositeObject* item = CreateGameObjectFromNode(params);
    Mem::PopMemProfile();
    m_items[slot] = item;

    Script::CStruct* tags = NewParams();
    tags->AddChecksum(kOwner, m_object->m_id);
    uint32_t script;
    if (params->GetChecksum(kDeathScript, &script, 0))
        tags->AddChecksum(kDeathScript, script);
    m_items[slot]->SetTagsFromStruct(tags);
    DeleteParams(tags);

    // In the object's colour.
    uint8_t* mine = ModelData(m_object);
    uint8_t* its = ModelData(item);
    if (mine && its) {
        uint32_t colour[4] = {Field<uint32_t>(mine, 0x1C), Field<uint32_t>(mine, 0x20), Field<uint32_t>(mine, 0x24),
                              Field<uint32_t>(mine, 0x28)};
        reinterpret_cast<Nx::CModel*>(its)->SetScale(reinterpret_cast<const Mth::Vector*>(colour)); // (0x00492EE0: four words set)
    }
    return item;
}

// 0x0051C160
int CSpecialItemComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript*)
{
    switch (name) {
    case kCreateSpecialItem: {
        // (Without them the original's slot and params name are what its argument slots held: the member
        // function's name and the params pointer.)
        int slot = static_cast<int>(name);
        uint32_t bone = 0;
        uint32_t itemParams = reinterpret_cast<uint32_t>(params);
        params->GetInteger(kIndex, &slot, 1);
        params->GetChecksum(kBone, &bone, 0);
        params->GetChecksum(kParams, &itemParams, 1);
        CCompositeObject* item = CreateItem(slot, Script::GetStructure(itemParams));
        Script::CStruct* lock = NewParams();
        lock->AppendStructureIfAny(params);
        lock->AddChecksum(kObjectId, m_object->m_id);
        item->GetComponent(kLockObj)->InitFromStructure(lock);
        DeleteParams(lock);
        struct {
            uint32_t params;
            uint32_t bone;
            uint8_t objectId;
            uint8_t slot;
            uint8_t pad[2];
        } created = {itemParams, bone, static_cast<uint8_t>(m_object->m_id), static_cast<uint8_t>(slot), {}};
        SendToServer(kMsgCreateSpecialItem, &created);
        return 1;
    }
    case kDestroySpecialItem: {
        int slot = static_cast<int>(reinterpret_cast<uint32_t>(params)); // (as above)
        params->GetInteger(kIndex, &slot, 1);
        DestroyItem(slot);
        struct {
            uint32_t unused[2]; // (not set by the original)
            uint8_t objectId;
            uint8_t slot;
            uint8_t pad[2];
        } destroyed = {{}, static_cast<uint8_t>(m_object->m_id), static_cast<uint8_t>(slot), {}};
        SendToServer(kMsgDestroySpecialItem, &destroyed);
        return 1;
    }
    case kDestroyAllSpecialItems:
        DestroyItems();
        return 1;
    case kSpecialItemExists: {
        int slot;
        if (params->GetInteger(kIndex, &slot, 0))
            return m_items[slot] != nullptr;
        for (CCompositeObject* item : m_items)
            if (item)
                return 1;
        return 0;
    }
    default:
        return 2;
    }
}

} // namespace Obj

REPLACE(0x0051BD80, Obj::CSpecialItemComponent::Create);
REPLACE(0x0051BF20, &Obj::CSpecialItemComponent::DestroyBody);
REPLACE(0x0051C130, &Obj::CSpecialItemComponent::Destroy);
REPLACE(0x0051BE00, &Obj::CSpecialItemComponent::DestroyItem);
REPLACE(0x0051BF00, &Obj::CSpecialItemComponent::DestroyItems);
REPLACE(0x0051BF80, &Obj::CSpecialItemComponent::CreateItem);
REPLACE(0x0051C160, &Obj::CSpecialItemComponent::CallMemberFunction);
