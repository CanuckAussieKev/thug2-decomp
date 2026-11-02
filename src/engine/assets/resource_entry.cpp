// Assets: the base class every asset type derives from, and the types' constructors.

#include <cstdint>

#include "engine/assets/resource_entry.h"

#include "engine/file/async.h"
#include "engine/script/node_manager.h"
#include "engine/core/arena_alloc.h"

namespace assets {

// 0x00407910
Resource::Resource() { dependents_.make_head(); }

// 0x00407810
Resource::~Resource() { dependents_.uncouple(); }

// 0x004075F0
int Resource::load_from_buffer(void*, uint32_t, bool, void*, void*)
{
    start_loading();
    return 0;
}
int Resource::read_in(const char*, bool, bool, void*, void*)
{
    start_loading();
    return 0;
}

// 0x00407600
int Resource::offload()
{
    set_unloaded();
    return 0;
}

// (the base's slot 7: a shared "return 0")
bool Resource::reload(const char*) { return false; }

// 0x00407610
bool Resource::is_loaded() const { return cur_phase_ == kLoaded; }

// 0x00407620
bool Resource::process_buffer() { return finish_loading(octets_, extent_); }

// 0x00407630
bool Resource::finish_loading(void* /*buffer*/, uint32_t /*size*/)
{
    set_loaded();
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
    set_aborted();
    return true;
}

// 0x004076D0
const char* Resource::fetch_title() const { return "Unnamed Asset"; }

// 0x004076E0
void Resource::change_contents(void* data) { content_ = data; }

// 0x004072F0
void Resource::pin_resident(bool permanent) { pinned_ = permanent; }

// 0x004076F0
void Resource::change_cluster(Resource* group) { bundle_ = group; }

// 0x00407700
void Resource::set_checksum(uint32_t checksum) { checksum_ = checksum; }

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
    memory::ListNode* node = memory::ListNode::create(asset);
    node->set_prev(dependents_.prev());
    node->set_next(&dependents_);
    dependents_.prev()->set_next(node);
    dependents_.set_prev(node);
}

// 0x00407890
void Resource::remove_dependent(Resource* asset)
{
    memory::ListNode* node = dependents_.next();
    if (node->data() == memory::ListNode::head_marker() || !node)
        return;
    while (node) {
        memory::ListNode* next = node->next()->data() == memory::ListNode::head_marker() ? nullptr : node->next();
        if (node->data() == asset) {
            memory::ListNode::destroy(node); // (0x004077D0)
            return;
        }
        node = next;
    }
}

// 0x00407710
void Resource::read_callback(file::AsyncFileHandle* /*file*/, int /*event*/, int size, void* asset, void* other)
{
    if (other)
        return;
    auto* self = static_cast<Resource*>(asset);
    self->extent_ = static_cast<uint32_t>(size);
    self->set_read();
}

// --- the types' constructors: the base's, then their fields ---

// 0x004071A0
AnimResource::AnimResource() = default;

// 0x00409220
SceneAsset::SceneAsset() = default;

// 0x004095F0
SceneCollisionAsset::SceneCollisionAsset() = default;

// 0x00409970
RigResource::RigResource() = default;

// 0x00409C40 (+0x50 .. +0x5C cleared; the archive left as allocated - zeroed)
SkinResource::SkinResource() = default;

// 0x0040A210
SkinCollisionAsset::SkinCollisionAsset() = default;

// 0x0040A580
TexDictAsset::TexDictAsset() = default;

// 0x00407A30
CutsceneResource::CutsceneResource() = default;

// 0x00408BB0
NodeTableResource::NodeTableResource() = default;

// 0x00408BF0
void NodeTableResource::make_owner()
{
    owned_ = true;
    level_nodes::node_manager_instance()->set_owner(this);
}

} // namespace assets
