// The fam component (GameObjects::FamComponent): see fam.h. From the disassembly of 0x0041D170-0x0041D6D4.

#include "engine/objects/fam.h"

#include <cstdint>
#include <cstdio>

#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/anim_blender.h"
#include "engine/audio/music_api.h"
#include "engine/audio/stream_commands.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

uint32_t g_fam_member_functions[18]{0xE5894C80, 0x0301D67C, 0, 0, 0,          0, 0x48AC2139, 0x0301D67C, 0,
                                    0,          0,          0, 0, 0x0301D67C, 0, 0,          0,          0};

namespace {

constexpr uint32_t kFamScript = 0x074253BB;            // fam_play_stream
constexpr uint32_t kStreamChecksum = 0x0572EA28;       // stream_checksum
constexpr uint32_t kVol = 0xF6A36814;                  // vol
constexpr uint32_t kPriority = 0x9D5923D8;             // priority
constexpr uint32_t kUsePosInfo = 0xC9B0DEB7;           // use_pos_info
constexpr uint32_t kStreamId = 0x8A68AB90;             // streamId
constexpr uint32_t kPlayAnim = 0x93516575;             // play_anim
constexpr uint32_t kTypeParam = 0x7321A8D6;            // type
constexpr uint32_t kId = 0x40C698AF;                   // id
constexpr uint32_t kPartialAnim = 0x659BF355;          // partialAnim
constexpr uint32_t kJawRotation = 0xC0BA0665;          // jawRotation
constexpr uint32_t kRemoveAnimController = 0x986D274E; // RemoveAnimController
constexpr int kDefaultPriority = 0x66;

assets::ResourceRegistry* asset_manager() { return assets::g_asset_manager; }

script::Struct* new_params()
{
    script::Struct* params = script::spawn_param_bag();
    return params ? params->prepare_all() : nullptr;
}
void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear(); // (the destructor)
        script::delete_struct(params);
    }
}

} // namespace

// 0x0041D200
memory::owner<ObjectModule*> __cdecl FamComponent::fabricate() { return new FamComponent; }

// (the constructor: inlined in fabricate in the original)
FamComponent::FamComponent()
{
    kind_tag_ = kType;
    fam_ = 0;
    feed_ = 0;
}

// 0x0041D170
void __cdecl FamComponent::publish_method_table()
{
    script::register_member_function_table(reinterpret_cast<void*>(kType), g_fam_member_functions, 0x18);
}

// 0x0041D190 (0x0041D1A0 the deleting one)
FamComponent::~FamComponent() {}

// 0x0041D2C0
void FamComponent::unload_fam()
{
    if (!fam_)
        return;
    script::stop_scripts_using_object_that_refer_to(entity_, kFamScript);
    script::Struct* params = new_params();
    params->put_name_key(kTypeParam, kPartialAnim);
    params->put_name_key(kId, kJawRotation);
    if (entity_)
        entity_->fetch_module(AnimBlenderComponent::kType)
            ->invoke_method_by_name(kRemoveAnimController, params, nullptr);
    delete_params(params);
    assets::ResourceRegistry* assets = asset_manager();
    if (assets::Resource* asset = assets->obtain_loaded_item(fam_, 0))
        assets->discard_loaded_item(asset);
    fam_ = 0;
}

// 0x0041D3E0
void FamComponent::load_anim_bank(uint32_t name, uint32_t anim_set)
{
    unload_fam();
    char file_name[32];
    std::snprintf(file_name, sizeof(file_name), "fam\\%08x.fam",
                  name); // (the original: the game's sprintf, 0x006234FE)
    debug::printf("loading %s\n", file_name);
    assets::ResourceRegistry* assets = asset_manager();
    int saved = assets->unknown18_;
    if (assets->load_anim(file_name, name, anim_set, false, false)) {
        debug::printf("loaded\n");
        fam_ = checksums::hash_from_text(file_name);
    }
    assets->set_unknown18(saved);
}

// 0x0041D480
bool FamComponent::play_fam(uint32_t name, bool stream_only, int volume, int priority)
{
    if (!audio::stream_exists(name))
        return false;
    if (feed_) {
        script::stop_scripts_using_object_that_refer_to(entity_, kFamScript);
        audio::halt_stream(feed_);
        feed_ = 0;
    }
    script::Struct* params = new_params();
    params->put_name_key(kStreamChecksum, name);
    if (volume)
        params->put_int(kVol, volume);
    if (!stream_only) {
        params->put_int(kUsePosInfo, 0);
        auto* blender = static_cast<AnimBlenderComponent*>(entity_->fetch_module(AnimBlenderComponent::kType));
        uint32_t anim_set = blender->get_anim_set();
        load_anim_bank(name, anim_set);
        uint32_t stream = audio::prime_stream(name, priority);
        params->put_name_key(kStreamId, stream);
        feed_ = stream;
        if (asset_manager()->get_asset_data(anim_set + name, 0))
            params->put_name_key(0u, kPlayAnim);
    }
    script::Script* script = script::spawn_script(kFamScript, params, 0, nullptr, 0, 0, false, false, false, false);
    script->entity_.assign_value(entity_);
    delete_params(params);
    return true;
}

// 0x0041D660
int FamComponent::obj_play_fam(script::Struct* params, script::Script*)
{
    uint32_t name;
    params->read_hash_value(0u, &name, 1);
    int volume = 0;
    params->fetch_int_value(kVol, &volume, 0);
    int priority = kDefaultPriority;
    params->fetch_int_value(kPriority, &priority, 0);
    return play_fam(name, false, volume, priority) ? 1 : 0;
}

// 0x0041D280
int FamComponent::obj_stop_last_fam(script::Struct*, script::Script*)
{
    if (feed_) {
        script::stop_scripts_using_object_that_refer_to(entity_, kFamScript);
        audio::halt_stream(feed_);
        feed_ = 0;
    }
    return 1;
}

} // namespace objects
