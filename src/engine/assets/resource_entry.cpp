// Assets: the base class every asset type derives from, and the types' constructors.

#include <cstdint>

#include "engine/assets/resource_entry.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/async.h"
#include "engine/script/node_manager.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "game/park_editing/park.h"
#include "engine/gfx/pc_texture.h"
#include "engine/gfx/particle_types.h"

namespace assets {

namespace {

extern void** const kAssetVtable;
extern void** const kDependentNodeVtable; // lists::Elem<Resource>
extern void** const kDependentHeadVtable; // lists::Foremost<Resource>

// The destructors behind the dependents list's vtables.
struct DependentNode : memory::ListNode {
    // 0x004077B0
    void destroy_body() { unlink_as(kDependentNodeVtable); }
    // 0x004077D0 (scalar deleting; the body inlined)
    DependentNode* teardown(uint8_t flags)
    {
        destroy_body();
        if (flags & 1)
            memory::Hub::get_pull()->dealloc(this);
        return this;
    }
    // 0x004078E0: the head's scalar deleting destructor (the node's body).
    DependentNode* destroy_head(uint8_t flags)
    {
        destroy_body();
        if (flags & 1)
            memory::Hub::get_pull()->dealloc(this);
        return this;
    }
};

static VtableSlot g_asset_vtable_slots[] = {stubs::return_zero, &park_editing::ParkTile::fetch_serial, &render::PcTexture::get_unknown_c,
        &assets::Resource::scalar_delete, &assets::Resource::start_loading, &assets::Resource::start_loading, &assets::Resource::offload,
        stubs::return_zero1, &assets::Resource::is_loaded, &assets::Resource::fetch_title, &assets::Resource::change_contents,
        &assets::Resource::pin_resident, &assets::Resource::process_buffer, &assets::Resource::finish_loading, &assets::Resource::cancel_now,
        stubs::do_nothing, &assets::Resource::set_loading, &assets::Resource::set_read, &assets::Resource::set_processing,
        &assets::Resource::set_loaded, &assets::Resource::set_unloaded, &assets::Resource::set_aborted};
void** const kAssetVtable = reinterpret_cast<void**>(g_asset_vtable_slots);
static VtableSlot g_dependent_node_vtable_slots[] = {&assets::DependentNode::teardown};
void** const kDependentNodeVtable = reinterpret_cast<void**>(g_dependent_node_vtable_slots); // lists::Elem<Resource>
static VtableSlot g_dependent_head_vtable_slots[] = {&assets::DependentNode::destroy_head};
void** const kDependentHeadVtable = reinterpret_cast<void**>(g_dependent_head_vtable_slots); // lists::Foremost<Resource>

static VtableSlot g_animation_vtable_slots[] = {&assets::AnimResource::fetch_kind, &park_editing::ParkTile::fetch_serial, &render::PcTexture::get_unknown_c,
        &assets::Resource::type_scalar_delete, &assets::AnimResource::load_from_buffer, &assets::AnimResource::read_in,
        &assets::AnimResource::offload, &assets::ReloadAdapter::reload, &assets::Resource::is_loaded, &assets::AnimResource::fetch_title,
        &assets::Resource::change_contents, &assets::Resource::pin_resident, &assets::Resource::process_buffer, &assets::AnimResource::finish_loading,
        &assets::Resource::cancel_now, &assets::AnimResource::get_free_buffer, &assets::Resource::set_loading, &assets::Resource::set_read,
        &assets::Resource::set_processing, &assets::Resource::set_loaded, &assets::Resource::set_unloaded, &assets::Resource::set_aborted};
void** const kAnimationVtable = reinterpret_cast<void**>(g_animation_vtable_slots);
static VtableSlot g_scene_vtable_slots[] = {&render::ParticleCounts::two_colours, &park_editing::ParkTile::fetch_serial, &render::PcTexture::get_unknown_c,
        &assets::Resource::type_scalar_delete, &assets::SceneAsset::load_from_buffer, &assets::SceneAsset::read_in,
        &assets::SceneAsset::offload, stubs::return_zero1, &assets::Resource::is_loaded, &assets::SceneAsset::fetch_title,
        &assets::Resource::change_contents, &assets::Resource::pin_resident, &assets::Resource::process_buffer, &assets::SceneAsset::finish_loading,
        &assets::Resource::cancel_now, stubs::do_nothing, &assets::Resource::set_loading, &assets::Resource::set_read,
        &assets::Resource::set_processing, &assets::Resource::set_loaded, &assets::Resource::set_unloaded, &assets::Resource::set_aborted};
void** const kSceneVtable = reinterpret_cast<void**>(g_scene_vtable_slots);
static VtableSlot g_scene_collision_vtable_slots[] = {&render::ParticleCounts::four_colours, &park_editing::ParkTile::fetch_serial,
        &render::PcTexture::get_unknown_c, &assets::Resource::type_scalar_delete, &assets::SceneCollisionAsset::load_from_buffer,
        &assets::SceneCollisionAsset::read_in, &assets::SceneCollisionAsset::offload, stubs::return_zero1, &assets::Resource::is_loaded,
        &assets::SceneCollisionAsset::fetch_title, &assets::Resource::change_contents, &assets::Resource::pin_resident,
        &assets::Resource::process_buffer, &assets::SceneCollisionAsset::finish_loading, &assets::Resource::cancel_now, stubs::do_nothing,
        &assets::Resource::set_loading, &assets::Resource::set_read, &assets::Resource::set_processing, &assets::Resource::set_loaded,
        &assets::Resource::set_unloaded, &assets::Resource::set_aborted};
void** const kSceneCollisionVtable = reinterpret_cast<void**>(g_scene_collision_vtable_slots);
static VtableSlot g_skeleton_vtable_slots[] = {&assets::RigResource::fetch_kind, &park_editing::ParkTile::fetch_serial, &render::PcTexture::get_unknown_c,
        &assets::Resource::type_scalar_delete, &assets::RigResource::load_from_buffer, &assets::RigResource::read_in,
        &assets::RigResource::offload, stubs::return_zero1, &assets::Resource::is_loaded, &assets::RigResource::fetch_title,
        &assets::Resource::change_contents, &assets::Resource::pin_resident, &assets::Resource::process_buffer,
        &assets::RigResource::finish_loading, &assets::Resource::cancel_now, &assets::RigResource::get_free_buffer,
        &assets::Resource::set_loading, &assets::Resource::set_read, &assets::Resource::set_processing, &assets::Resource::set_loaded,
        &assets::Resource::set_unloaded, &assets::Resource::set_aborted};
void** const kSkeletonVtable = reinterpret_cast<void**>(g_skeleton_vtable_slots);
static VtableSlot g_skinned_model_vtable_slots[] = {&assets::SkinResource::fetch_kind, &park_editing::ParkTile::fetch_serial, &render::PcTexture::get_unknown_c,
        &assets::Resource::type_scalar_delete, &assets::SkinResource::load_from_buffer, &assets::SkinResource::read_in,
        &assets::SkinResource::offload, stubs::return_zero1, &assets::Resource::is_loaded, &assets::SkinResource::fetch_title,
        &assets::Resource::change_contents, &assets::Resource::pin_resident, &assets::Resource::process_buffer, &assets::SkinResource::finish_loading,
        &assets::SkinResource::cancel_now, &assets::SkinResource::get_free_buffer, &assets::Resource::set_loading, &assets::SkinResource::set_read,
        &assets::Resource::set_processing, &assets::Resource::set_loaded, &assets::Resource::set_unloaded, &assets::Resource::set_aborted};
void** const kSkinnedModelVtable = reinterpret_cast<void**>(g_skinned_model_vtable_slots);
static VtableSlot g_skin_collision_vtable_slots[] = {&assets::SkinCollisionAsset::fetch_kind, &park_editing::ParkTile::fetch_serial,
        &render::PcTexture::get_unknown_c, &assets::Resource::type_scalar_delete, &assets::SkinCollisionAsset::load_from_buffer,
        &assets::SkinCollisionAsset::read_in, &assets::SkinCollisionAsset::offload, stubs::return_zero1, &assets::Resource::is_loaded,
        &assets::SkinCollisionAsset::fetch_title, &assets::Resource::change_contents, &assets::Resource::pin_resident,
        &assets::Resource::process_buffer, &assets::SkinCollisionAsset::finish_loading, &assets::Resource::cancel_now, stubs::do_nothing,
        &assets::Resource::set_loading, &assets::Resource::set_read, &assets::Resource::set_processing, &assets::Resource::set_loaded,
        &assets::Resource::set_unloaded, &assets::Resource::set_aborted};
void** const kSkinCollisionVtable = reinterpret_cast<void**>(g_skin_collision_vtable_slots);
static VtableSlot g_texture_dictionary_vtable_slots[] = {&render::ParticleCounts::three_colours, &park_editing::ParkTile::fetch_serial,
        &render::PcTexture::get_unknown_c, &assets::Resource::type_scalar_delete, &assets::TexDictAsset::load_from_buffer,
        &assets::TexDictAsset::read_in, &assets::TexDictAsset::offload, stubs::return_zero1, &assets::Resource::is_loaded,
        &assets::TexDictAsset::fetch_title, &assets::Resource::change_contents, &assets::Resource::pin_resident, &assets::Resource::process_buffer,
        &assets::TexDictAsset::finish_loading, &assets::Resource::cancel_now, stubs::do_nothing, &assets::Resource::set_loading,
        &assets::Resource::set_read, &assets::Resource::set_processing, &assets::Resource::set_loaded, &assets::Resource::set_unloaded,
        &assets::Resource::set_aborted};
void** const kTextureDictionaryVtable = reinterpret_cast<void**>(g_texture_dictionary_vtable_slots);
static VtableSlot g_cutscene_vtable_slots[] = {&assets::CutsceneResource::fetch_kind, &park_editing::ParkTile::fetch_serial, &render::PcTexture::get_unknown_c,
        &assets::Resource::type_scalar_delete, &assets::Resource::start_loading, &assets::CutsceneResource::read_in,
        &assets::CutsceneResource::offload, &assets::ReloadAdapter::reload, &assets::CutsceneResource::is_loaded,
        &assets::CutsceneResource::fetch_title, &assets::Resource::change_contents, &assets::Resource::pin_resident, &assets::Resource::process_buffer,
        &assets::Resource::finish_loading, &assets::Resource::cancel_now, stubs::do_nothing, &assets::Resource::set_loading,
        &assets::Resource::set_read, &assets::Resource::set_processing, &assets::Resource::set_loaded, &assets::Resource::set_unloaded,
        &assets::Resource::set_aborted};
void** const kCutsceneVtable = reinterpret_cast<void**>(g_cutscene_vtable_slots);
static VtableSlot g_node_array_vtable_slots[] = {&assets::NodeTableResource::fetch_kind, &park_editing::ParkTile::fetch_serial,
        &render::PcTexture::get_unknown_c, &assets::Resource::type_scalar_delete, &assets::NodeTableResource::load_from_buffer,
        &assets::NodeTableResource::read_in, &assets::NodeTableResource::offload, &assets::ReloadAdapter::reload, &assets::Resource::is_loaded,
        &assets::NodeTableResource::fetch_title, &assets::Resource::change_contents, &assets::Resource::pin_resident, &assets::Resource::process_buffer,
        &assets::NodeTableResource::finish_loading, &assets::Resource::cancel_now, &assets::AnimResource::get_free_buffer, &assets::Resource::set_loading,
        &assets::Resource::set_read, &assets::Resource::set_processing, &assets::Resource::set_loaded, &assets::Resource::set_unloaded,
        &assets::Resource::set_aborted};
void** const kNodeArrayVtable = reinterpret_cast<void**>(g_node_array_vtable_slots);

Resource* construct_type(Resource* asset, void** vtable)
{
    asset->make_new();
    asset->vtable_ = vtable;
    return asset;
}
// The extra field most types have at +0x4C.
void clear_extra(Resource* asset) { *reinterpret_cast<void**>(asset + 1) = nullptr; }

} // namespace

// 0x00407910
Resource* Resource::make_new()
{
    vtable_ = kAssetVtable;
    dependents_.data = reinterpret_cast<void*>(-1);
    dependents_.priority = 0;
    dependents_.next = &dependents_;
    dependents_.prev = &dependents_;
    dependents_.vtable = kDependentHeadVtable;
    bundle_ = nullptr;
    cur_phase_ = kUnloaded;
    file_ref_ = nullptr;
    octets_ = nullptr;
    extent_ = 0;
    callback_ = nullptr;
    return this;
}

// 0x00407810
void Resource::destruct()
{
    vtable_ = kAssetVtable;
    dependents_.unlink_as(kDependentNodeVtable);
}

// 0x004093E0 (a jump to 0x00407810, the types' destructor body)
void Resource::destruct_type()
{
    destruct();
}

// 0x00407950
Resource* Resource::scalar_delete(uint8_t flags)
{
    destruct();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00407AF0
Resource* Resource::type_scalar_delete(uint8_t flags)
{
    destruct_type();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004075F0
int Resource::start_loading(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
{
    get_call<int>(16); // set_loading
    return 0;
}

// 0x00407600
int Resource::offload()
{
    get_call<bool>(20); // set_unloaded
    return 0;
}

// 0x00407610
bool Resource::is_loaded() const
{
    return cur_phase_ == kLoaded;
}

// 0x00407620
bool Resource::process_buffer()
{
    return v_finish_loading(octets_, extent_);
}

// 0x00407630
bool Resource::finish_loading(void* /*buffer*/, uint32_t /*size*/)
{
    get_call<bool>(19); // set_loaded
    return true;
}

// 0x00407640
bool Resource::cancel_now()
{
    if (file_ref_) {
        file_ref_->get_cancel();
        file::async_close(file_ref_);
        file_ref_ = nullptr;
    }
    get_call<bool>(21); // set_aborted
    return true;
}

// 0x004076D0
const char* Resource::fetch_title() const
{
    return "Unnamed Asset";
}

// 0x004076E0
void Resource::change_contents(void* data)
{
    content_ = data;
}

// 0x004072F0
void Resource::pin_resident(bool permanent)
{
    pinned_ = permanent;
}

// 0x004076F0
void Resource::change_cluster(Resource* group)
{
    bundle_ = group;
}

// 0x00407700
void Resource::set_checksum(uint32_t checksum)
{
    checksum_ = checksum;
}

// 0x004075D0
void Resource::change_hook(LoadCallback callback, void* first_word, void* data1)
{
    callback_ = callback;
    callback_data0_ = first_word;
    callback_data1_ = data1;
}

// 0x00407670
int Resource::set_loading()
{
    cur_phase_ = kLoading;
    return 1;
}

// 0x00407680
bool Resource::set_read()
{
    cur_phase_ = kRead;
    return true;
}

// 0x00407690
bool Resource::set_processing()
{
    cur_phase_ = kProcessing;
    return true;
}

// 0x004076A0
bool Resource::set_loaded()
{
    cur_phase_ = kLoaded;
    return true;
}

// 0x004076B0
bool Resource::set_unloaded()
{
    cur_phase_ = kUnloaded;
    return true;
}

// 0x004076C0
bool Resource::set_aborted()
{
    cur_phase_ = kAborted;
    return true;
}

// 0x00407840
void Resource::add_dependent(Resource* asset)
{
    auto* node = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
    if (node) {
        node->vtable = kDependentNodeVtable;
        node->data = asset;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    node->prev = dependents_.prev;
    node->next = &dependents_;
    dependents_.prev->next = node;
    dependents_.prev = node;
}

// 0x00407890
void Resource::remove_dependent(Resource* asset)
{
    memory::ListNode* node = dependents_.next;
    if (node->data == reinterpret_cast<void*>(-1) || !node)
        return;
    while (node) {
        memory::ListNode* next = node->next->data == reinterpret_cast<void*>(-1) ? nullptr : node->next;
        if (node->data == asset) {
            reinterpret_cast<void(__thiscall*)(memory::ListNode*, int)>(node->vtable[0])(node, 1);
            return;
        }
        node = next;
    }
}

// 0x00407710
void __cdecl Resource::read_callback(file::AsyncFileHandle* /*file*/, int /*event*/, int size, void* asset, void* other)
{
    if (other)
        return;
    auto* self = static_cast<Resource*>(asset);
    self->extent_ = static_cast<uint32_t>(size);
    self->get_call<bool>(17); // set_read
}

// 0x004071A0
Resource* AssetConstructors::get_animation()
{
    construct_type(this, kAnimationVtable);
    clear_extra(this);
    return this;
}

// 0x00409220
Resource* AssetConstructors::get_scene()
{
    construct_type(this, kSceneVtable);
    clear_extra(this);
    return this;
}

// 0x004095F0
Resource* AssetConstructors::scene_collision()
{
    construct_type(this, kSceneCollisionVtable);
    clear_extra(this);
    return this;
}

// 0x00409970
Resource* AssetConstructors::get_skeleton()
{
    return construct_type(this, kSkeletonVtable);
}

// 0x00409C40
Resource* AssetConstructors::skinned_model()
{
    make_new();
    auto* extra = reinterpret_cast<uint32_t*>(this + 1);
    extra[1] = 0; // +0x50 .. +0x5C
    extra[2] = 0;
    extra[3] = 0;
    extra[4] = 0;
    vtable_ = kSkinnedModelVtable;
    return this;
}

// 0x0040A210
Resource* AssetConstructors::skin_collision()
{
    construct_type(this, kSkinCollisionVtable);
    clear_extra(this);
    return this;
}

// 0x0040A580
Resource* AssetConstructors::texture_bank()
{
    construct_type(this, kTextureDictionaryVtable);
    clear_extra(this);
    return this;
}

// 0x00407A30
Resource* AssetConstructors::get_cutscene()
{
    return construct_type(this, kCutsceneVtable);
}

// 0x00408BB0
Resource* AssetConstructors::get_node_array()
{
    construct_type(this, kNodeArrayVtable);
    clear_extra(this);
    return this;
}

// 0x00408BF0
void AssetConstructors::make_node_array_owner()
{
    reinterpret_cast<uint8_t*>(this)[0x60] = 1;
    level_nodes::node_manager_instance()->set_owner(this);
}

} // namespace Ass
