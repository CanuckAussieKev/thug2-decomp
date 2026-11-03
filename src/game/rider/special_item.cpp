// The special item component: the items an object carries. See special_item.h.

#include "game/rider/special_item.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "wire/message_ids.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "wire/skater_messages.h"
#include "formats/byte_stream.h"
#include "engine/objects/node_objects.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/model.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/model_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"

namespace objects {

namespace {

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

// The object marked dead (its vtable +0x18: a composite object tells its components first).
void flag_as_dead(GameEntity* object) { object->flag_as_dead(); }

// An object's model's data (its model component's +0x208), or null.
render::RenderModel* get_model_data(GameEntity* object)
{
    auto* model = core::down_cast<MeshInstanceModule>(object->fetch_module(kModel));
    return model ? model->mesh_instance().get() : nullptr;
}

// A message to the server, in a network game.
void send_to_server(online::MessageId id, const online::SpecialItemMessage& item)
{
    auto* manager = online::net_game_state().online_hub();
    if (!manager->in_net_game())
        return;
    net::NetMessageInfo msg;
    auto* client = core::down_cast<net::Guest>(manager->fetch_patron(0));
    std::array<uint8_t, online::kSpecialItemSize> data{};
    util::ByteWriter writer(data.data(), sizeof(data));
    online::write_special_item(writer, item);
    msg.set_content(data.data());
    msg.set_byte_count(sizeof(data));
    msg.set_ident(id);
    client->send_to_host(&msg);
}

} // namespace

// 0x0051BD80
gsl::owner<ObjectModule*> PickupItemHandler::fabricate() { return new PickupItemHandler; }

// (the constructor: inlined in fabricate in the original)
PickupItemHandler::PickupItemHandler()
{
    set_kind_tag(kType);
    core::at(items_, 0) = nullptr;
    core::at(items_, 1) = nullptr;
}

// 0x0051BF20 (0x0051C130 the deleting one)
PickupItemHandler::~PickupItemHandler()
{
    for (int slot = 0; slot < kItems; ++slot)
        destroy_item(slot);
}

// 0x0051BE00
void PickupItemHandler::destroy_item(int slot)
{
    if (!core::at(items_, slot))
        return;
    script::StructPtr tags = script::make_struct();
    core::at(items_, slot)->copy_tags_to(tags.get());
    uint32_t script = 0;
    if (tags->read_hash_value(kOnDestroyScript, &script, 0))
        script::launch_script(script, nullptr, core::at(items_, slot), false, nullptr);
    tags.reset();
    flag_as_dead(core::at(items_, slot));
    core::at(items_, slot) = nullptr;
}

// 0x0051BF00
void PickupItemHandler::destroy_items()
{
    for (int slot = 0; slot < kItems; ++slot)
        destroy_item(slot);
}

// 0x0051BF80
GameEntity* PickupItemHandler::create_item(int slot, script::Struct* params)
{
    if (core::at(items_, slot))
        destroy_item(slot);
    uint32_t id = 0;
    if (params->read_hash_value(kId, &id, 0)) {
        id += entity()->ident();
        params->put_name_key(checksums::Name{kId}, id);
    }
    memory::push_mem_profile("Game Objects");
    GameEntity* item = create_game_object_from_node(params);
    memory::pop_mem_profile();
    core::at(items_, slot) = item;

    script::StructPtr tags = script::make_struct();
    tags->put_name_key(checksums::Name{kOwner}, entity()->ident());
    uint32_t script = 0;
    if (params->read_hash_value(kOnDestroyScript, &script, 0))
        tags->put_name_key(checksums::Name{kOnDestroyScript}, script);
    core::at(items_, slot)->set_tags_from_struct(tags.get());
    tags.reset();

    // In the object's colour.
    const render::RenderModel* mine = get_model_data(entity());
    render::RenderModel* its = get_model_data(item);
    if (mine && its) {
        const maths::Vec4 colour = mine->size_factor();
        its->change_scale(&colour); // (0x00492EE0: four words set)
    }
    return item;
}

// 0x0051C160
int PickupItemHandler::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    switch (name) {
    case kCreateSpecialItem: {
        // (Without them the original's slot and params name are what its argument slots held: the member
        // function's name and the params pointer - here the name, and no params.)
        int slot = static_cast<int>(name);
        uint32_t bone = 0;
        uint32_t item_params = 0;
        params->fetch_int_value(kIndex, &slot, 1);
        params->read_hash_value(kBone, &bone, 0);
        params->read_hash_value(kParams, &item_params, 1);
        GameEntity* item = create_item(slot, script::read_sub_struct(item_params));
        script::StructPtr lock = script::make_struct();
        lock->append_structure_if_any(params);
        lock->put_name_key(checksums::Name{kObjectId}, entity()->ident());
        item->fetch_module(kLockObj)->configure_from_data(lock.get());
        lock.reset();
        online::SpecialItemMessage created;
        created.params = item_params;
        created.bone = bone;
        created.object_id = static_cast<uint8_t>(entity()->ident());
        created.slot = static_cast<int8_t>(slot);
        send_to_server(online::MessageId::kCreateSpecialItem, created);
        return 1;
    }
    case kDestroySpecialItem: {
        int slot = 0; // (the original's: the params pointer's slot)
        params->fetch_int_value(kIndex, &slot, 1);
        destroy_item(slot);
        online::SpecialItemMessage destroyed; // (params and bone: not set by the original)
        destroyed.object_id = static_cast<uint8_t>(entity()->ident());
        destroyed.slot = static_cast<int8_t>(slot);
        send_to_server(online::MessageId::kDestroySpecialItem, destroyed);
        return 1;
    }
    case kDestroyAllSpecialItems:
        destroy_items();
        return 1;
    case kSpecialItemExists: {
        int slot = 0;
        if (params->fetch_int_value(kIndex, &slot, 0))
            return core::at(items_, slot) != nullptr;
        for (const GameEntity* item : items_)
            if (item)
                return 1;
        return 0;
    }
    default:
        return 2;
    }
}

} // namespace objects
