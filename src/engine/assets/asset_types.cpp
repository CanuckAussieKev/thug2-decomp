// The asset types: animations, cutscenes, node arrays, scenes, scene collision, skeletons, skinned models, skin
// collision and texture dictionaries - how each is read and turned into the engine's data.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "engine/assets/resource_entry.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/file/async.h"
#include "engine/file/packs.h"
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_mesh.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/engine_glue.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/entity.h"
#include "engine/script/node_manager.h"
#include "engine/script/qb.h"
#include "engine/script/script_startup.h"
#include "engine/script/param_struct.h"
#include <gsl/pointers>

namespace assets {

namespace {
uint32_t g_params_in_use;

using script::Struct;

// --- the engine's loaders ---------------------------------------------------------------------------------
// gfx::JointClipFrames (0x3C bytes): 0x00482AF0 the constructor, 0x00482B30 the destructor, 0x004856C0 Load.
gsl::owner<void*> construct_anim_frames(void* frames) { return new (frames) gfx::JointClipFrames; }
void destroy_anim_frames(void* frames) { static_cast<gfx::JointClipFrames*>(frames)->~JointClipFrames(); }
bool load_anim_frames(void* frames, void* buffer, uint32_t size)
{
    return static_cast<gfx::JointClipFrames*>(frames)->read_in(static_cast<const uint8_t*>(buffer), size, true);
}
// The skeleton data (0x598 bytes): 0x004A2C20 the constructor, 0x004A39C0 Load.

bool load_skeleton_data(void* skeleton, void* buffer, uint32_t size)
{
    return static_cast<gfx::RigData*>(skeleton)->read_in(static_cast<const uint32_t*>(buffer), size, true);
}
// Scripts: 0x005A5990 parses a .qb file's contents, 0x005A59B0 unloads one (by its name's checksum).
void parse_qb_file(const char* name, void* buffer)
{
    script::load_qb_file_data(name, static_cast<uint8_t*>(buffer), 1, 1);
}
void unload_qb_file(uint32_t name) { script::unload_qb_file(name); }
// Scenes (Nx): the file name, made from the buffer (with the textures), unloaded.
void build_scene_file_path(char* out, const char* name, uint8_t flag)
{
    render::engine::build_scene_file_path(out, name, flag);
}
void* make_scene(const char* name, void* buffer, uint32_t size, void* textures, uint8_t a, uint8_t b, uint8_t c,
                 uint8_t d)
{
    return render::load_named_scene_from_memory(name, static_cast<const uint8_t*>(buffer), size,
                                                static_cast<render::TextureSet*>(textures), a != 0, b != 0, c != 0, d);
}
void unload_scene(void* scene, int flag)
{
    render::engine::unload_scene(static_cast<render::WorldScene*>(scene), flag);
}
// Scene collision: its file name, added to a scene, the scene's collision, removed.
void collision_file_name(char* out, const char* name, uint8_t flag)
{
    (void)flag; // (not read)
    render::make_collision_file_name(out, name);
}
void add_collision(void* scene, void* buffer, uint32_t size, uint8_t flag)
{
    static_cast<render::WorldScene*>(scene)->add_collision_data(static_cast<uint8_t*>(buffer), size, flag);
}
void* scene_collision(void* scene) { return static_cast<render::WorldScene*>(scene)->fetch_static_collision(0); }
void detach_hit_mesh(void* scene) { static_cast<render::WorldScene*>(scene)->unload_collision(); }
// Skinned models: whether a name is a model with a .cas file (its base name out), the model made, unloaded.
bool split_skin_name(const char* name, char* base) { return render::engine::split_skin_name(name, base); }
gsl::owner<void*> make_skinned_model(uint32_t checksum, void* buffer, uint32_t size, void* cas, uint32_t cas_size,
                         render::TextureSet* textures, int value, uint8_t has_cas, uint8_t flag)
{
    return render::engine::load_model_from_memory(checksum, static_cast<const uint8_t*>(buffer), static_cast<int>(size),
                                                  cas, cas_size, textures, value, has_cas, flag);
}
void unload_skinned_model(void* model, int flag)
{
    (void)flag; // 0x0048C080 is a jump to the engine's virtual delete (its second argument isn't read)
    gsl::owner<render::SubMesh*> mesh = memory::adopt<render::SubMesh*>(model);
    delete mesh;
}
// Skin collision: its file name, added to a model, removed.
void skin_collision_file_name(char* out, const char* name) { render::skin_collision_file_name(out, name); }
void add_skin_collision(void* model, void* buffer, uint32_t size)
{
    static_cast<render::SubMesh*>(model)->add_collision(static_cast<uint8_t*>(buffer), size);
}
void remove_skin_collision(void* model) { static_cast<render::SubMesh*>(model)->detach_hit_mesh(); }
// Texture dictionaries: the file name, made, unloaded.
void tex_dict_file_name(char* out, const char* name) { render::tex_dict_file_name(out, name); }
void* make_tex_dict(uint32_t checksum, void* buffer, uint32_t size, uint8_t a, int value, uint8_t b, uint8_t c)
{
    return render::load_tex_dict_from_memory(checksum, static_cast<const uint8_t*>(buffer), size, a,
                                             static_cast<uint32_t>(value), b, c);
}
void unload_tex_dict(void* textures) { render::unload_tex_dict(static_cast<render::TextureSet*>(textures)); }

// --- shared steps ---------------------------------------------------------------------------------------------
std::array<uint8_t, 0x180> g_params_pool; // 4 blocks of 0x60 bytes
std::array<uint8_t, 0x180>& params_pool() { return g_params_pool; }
uint32_t& params_in_use() { return g_params_in_use; }

// strcpy (no bounds, as the original's).
void copy_string(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
// The file opened (null: false) and read into a new buffer; an async read calls back when it's done.
bool read_file(Resource* asset, const char* file, bool async, uint32_t* size)
{
    asset->file_ref_ = file::async_open(file, !async, 100);
    if (!asset->file_ref_)
        return false;
    *size = static_cast<uint32_t>(asset->file_ref_->get_file_size());
    asset->octets_ = memory::alloc_block(*size);
    return true;
}
void start_reading(Resource* asset, file::AsyncFileHandle* file, void* buffer, uint32_t size, bool async)
{
    if (async) {
        file->callback_mask_ = 0;
        file->callback_mask_ |= 1u << file::kOpReadFile;
        file->change_hook(&Resource::read_callback, asset, nullptr);
    }
    file->get_take_in(buffer, 1, static_cast<int>(size));
}
// An archive file through Pip: its lowercased name's checksum kept (`lowered` false: the name is already).
void read_archive(Resource* asset, uint32_t* archive, const char* file, uint32_t* size, bool lower)
{
    std::array<char, 256> lowered{};
    const char* key = file;
    if (lower) {
        copy_string(lowered.data(), file);
        text::to_lower_text(lowered.data());
        key = lowered.data();
    }
    *archive = checksums::hash_from_text(key);
    asset->octets_ = packs::read_in(file, false, nullptr, nullptr, nullptr);
    *size = static_cast<uint32_t>(packs::get_file_size(file));
}
// A blocking load finishes at once.
void finish_now(Resource* asset, uint32_t size)
{
    asset->extent_ = size;
    asset->process_buffer();
}
// load_from_buffer's copy: the data copied into a buffer of the asset's own.
void* copy_data(Resource* asset, void* data, uint32_t size)
{
    asset->octets_ = memory::alloc_block(size);
    std::memcpy(asset->octets_, data, size);
    return asset->octets_;
}
void close_file(file::AsyncFileHandle*& file)
{
    if (file) {
        file::async_close(file);
        file = nullptr;
    }
}
void free_malloc_buffer(void*& buffer)
{
    if (buffer) {
        memory::free_malloc(buffer);
        buffer = nullptr;
    }
}
void* group_data(Resource* asset) { return asset->bundle_->get_data(); }
const char* platform_name(const char* name, char* out)
{
    std::snprintf(out, 256, "%s.%s", name, render::platform_suffix());
    return out;
}
void copy_words(void* to, const void* from, int words) { std::memcpy(to, from, 4 * static_cast<size_t>(words)); }

} // namespace

// 0x00407730
void* alloc_load_params(uint32_t /*size*/)
{
    for (int i = 0; i < 4; ++i) {
        if (!(params_in_use() & (1u << i))) {
            params_in_use() |= 1u << i;
            return params_pool().data() + 0x60 * i;
        }
    }
    return nullptr;
}

// 0x00407770
void free_load_params(void* params)
{
    for (int i = 0; i < 4; ++i) {
        if (params_pool().data() + 0x60 * i == params) {
            params_in_use() &= ~(1u << i);
            return;
        }
    }
}

// 0x00408A70
bool refresh_resource(Resource* asset, const char* name)
{
    asset->offload();
    return asset->read_in(name, false, false, nullptr, nullptr) == 0;
}

// --- animation ------------------------------------------------------------------------------------------------

// 0x004072E0
int AnimResource::fetch_kind() const { return kAnimation; }
// 0x004072D0
const char* AnimResource::fetch_title() const { return "Animation"; }

// 0x004071C0
int AnimResource::load_from_buffer(void* data, uint32_t size, bool copy, void* /*params*/, void* /*structure*/)
{
    memory::push_mem_profile("Animation Sequence processing");
    start_loading();
    if (copy) {
        memory::push_mem_profile("anim from data stream");
        data = copy_data(this, data, size);
        memory::pop_mem_profile();
    }
    finish_loading(data, size);
    memory::pop_mem_profile();
    return 0;
}

// 0x00407300
int AnimResource::read_in(const char* name, bool async, bool use_archive, void* /*params*/, void* /*structure*/)
{
    start_loading();
    script::fetch_int_value(0x25DC7904u); // (a debug leftover: looked up and not used)
    memory::push_mem_profile(name);
    std::array<char, 256> file{};
    platform_name(name, file.data());
    uint32_t size = 0;
    if (use_archive) {
        read_archive(this, &archive_, file.data(), &size, true);
    } else {
        if (!read_file(this, file.data(), async, &size))
            return -1;
        start_reading(this, file_ref_, octets_, size, async);
    }
    if (!async)
        finish_now(this, size); // (the original reads the checksum first and doesn't use it)
    memory::pop_mem_profile();
    return 0;
}

// 0x004074F0
bool AnimResource::finish_loading(void* buffer, uint32_t size)
{
    void* memory = memory::alloc(0x3C);
    void* frames = memory ? construct_anim_frames(memory) : nullptr;
    if (!load_anim_frames(frames, buffer, size)) { // (called on a failed allocation too, as in the original)
        script::fetch_int_value(0x25DC7904u);
        if (frames) {
            destroy_anim_frames(frames);
            memory::Hub::get_pull()->dealloc(frames);
        }
        frames = nullptr;
    }
    change_contents(frames);
    close_file(file_ref_);
    return Resource::finish_loading(buffer, size);
}

// 0x004074A0
int AnimResource::offload()
{
    if (get_data()) {
        if (void* frames = get_data()) {
            destroy_anim_frames(frames);
            memory::Hub::get_pull()->dealloc(frames);
        }
        change_contents(nullptr);
    }
    free_buffer();
    return Resource::offload();
}

// 0x00407290
void AnimResource::free_buffer()
{
    if (archive_) {
        packs::offload(archive_);
        archive_ = 0;
        octets_ = nullptr;
        return;
    }
    if (octets_)
        memory::free_malloc(octets_);
    archive_ = 0;
    octets_ = nullptr;
}

// --- cutscene -------------------------------------------------------------------------------------------------

// 0x00408AC0
int CutsceneResource::fetch_kind() const { return kCutscene; }
// 0x00408AB0
const char* CutsceneResource::fetch_title() const { return "Cutscene"; }

// 0x00408AD0 (no start_loading: the state stays as it was)
int CutsceneResource::read_in(const char* name, bool async, bool /*use_archive*/, void* params, void* /*structure*/)
{
    memory::push_mem_profile(name);
    auto cutscene = memory::make_owned<objects::CinematicTable>();
    std::array<char, 256> file{};
    platform_name(name, file.data());
    if (!cutscene->read_in(file.data(), 1, async, static_cast<const objects::CutsceneSkater*>(params)))
        return -1;
    change_contents(cutscene);
    memory::pop_mem_profile();
    return 0;
}

// 0x00408A40
int CutsceneResource::offload()
{
    if (void* cutscene = get_data()) {
        gsl::owner<objects::CinematicTable*> table = memory::adopt<objects::CinematicTable*>(cutscene);
        delete table;
        change_contents(nullptr);
    }
    return 0;
}

// 0x00408AA0
bool CutsceneResource::is_loaded() const { return static_cast<objects::CinematicTable*>(get_data())->is_loaded(); }

// --- node array -----------------------------------------------------------------------------------------------

// 0x00408BE0
int NodeTableResource::fetch_kind() const { return owned_ ? kOwnedNodeArray : kNodeArray; }
// 0x00408BD0
const char* NodeTableResource::fetch_title() const { return "Node Array"; }

// 0x00408EA0
int NodeTableResource::load_from_buffer(void* data, uint32_t size, bool /*copy*/, void* params, void* /*structure*/)
{
    start_loading();
    args_ = static_cast<char*>(alloc_load_params(0x40));
    if (params)
        copy_words(args_, params, 0x10);
    finish_loading(data, size);
    return 0;
}

// 0x00408D10
int NodeTableResource::read_in(const char* name, bool async, bool use_archive, void* params, void* /*structure*/)
{
    start_loading();
    args_ = static_cast<char*>(alloc_load_params(0x40));
    if (params)
        copy_words(args_, params, 0x10);
    copy_string(args_, name);
    memory::enter_heap_scope(memory::top_down_heap());
    uint32_t size = 0;
    if (use_archive) {
        read_archive(this, &archive_, name, &size, true);
    } else {
        if (!read_file(this, name, async, &size))
            return -1; // (with the heap context still pushed, as in the original)
        start_reading(this, file_ref_, octets_, size, async);
    }
    if (!async)
        finish_now(this, size);
    memory::leave_heap_scope();
    return 0;
}

// 0x00408C10
bool NodeTableResource::finish_loading(void* buffer, uint32_t size)
{
    source_path_ = checksums::hash_from_text(args_);
    std::array<char, 128> array_name{};
    level_nodes::make_node_array_name(args_, array_name.data());
    parse_qb_file(args_, buffer);
    level_nodes::node_manager_instance()->add_node_array(level_nodes::node_array_checksum(args_), owned_);
    script::free_script_checksum_names();
    script::Array* nodes = script::fetch_list(array_name.data());
    array_name_ = checksums::hash_from_text(array_name.data());
    change_contents(nodes);
    nodes_ = static_cast<script::Array*>(get_data());
    close_file(file_ref_);
    free_buffer();
    free_load_params(args_);
    args_ = nullptr;
    return Resource::finish_loading(buffer, size);
}

// 0x00408F00
int NodeTableResource::offload()
{
    auto* nodes = nodes_;
    for (uint32_t i = 0; i < nodes->extent_; ++i) {
        uint32_t id = 0;
        nodes->struct_at(i)->read_hash_value(0xA1DC81F9u, &id, 0); // name
        if (objects::Entity* object = objects::find_object(id))
            object->flag_as_dead();
        nodes = nodes_;
    }
    if (level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get())
        manager->remove_node_array(array_name_);
    if (get_data())
        unload_qb_file(source_path_);
    free_buffer();
    return Resource::offload();
}

// 0x004090D0
void* NodeTableResource::build_node_table(uint32_t name, int count)
{
    gsl::owner<script::Array*> array = script::alloc_script_array();
    if (array)
        new (array) script::Array;
    array->configure_size_kind(count, script::kQbvalStruct);
    for (int i = 0; i < count; ++i) {
        gsl::owner<Struct*> node = script::spawn_param_bag();
        if (node)
            new (node) script::Struct;
        array->set_cell(i, script::Cell::of(node));
    }
    if (script::SymbolTableEntry* old = script::find_symbol(name))
        script::clean_up_and_remove_symbol(old);
    script::SymbolTableEntry* symbol = script::create_symbol(name);
    symbol->value.set_array(array);
    symbol->type = script::kQbvalArray;
    array_name_ = name;
    script::Array* const table = array; // (the symbol's now)
    nodes_ = table;
    return table;
}

// --- scene ----------------------------------------------------------------------------------------------------

// 0x004093D0
const char* SceneAsset::fetch_title() const { return "Scene"; }

// 0x004092D0
void SceneAsset::set_up_params(void* params, void* structure)
{
    auto made = memory::construct_in<SceneLoadParams>(alloc_load_params(sizeof(SceneLoadParams)));
    args_ = made; // (a record of the load-parameter pool, given back once loaded)
    args_->textures = 0;
    args_->flags = {{1, 0, 0, 0}};
    if (params)
        copy_words(args_, params, 0x12);
    if (auto* s = static_cast<Struct*>(structure)) {
        s->read_hash_value(0xDC8116B2u, &args_->textures, 0);
        args_->flags = {{static_cast<uint8_t>(s->has_bare_flag(0x9E6EA452u)), static_cast<uint8_t>(s->has_bare_flag(0xB5BD28D8u)),
                         static_cast<uint8_t>(s->has_bare_flag(0x3D559E7Du)), static_cast<uint8_t>(s->has_bare_flag(0x417149C8u))}};
    }
}

// 0x00409520
int SceneAsset::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    start_loading();
    set_up_params(params, structure);
    if (copy) {
        memory::push_mem_profile("Scene from data stream");
        data = copy_data(this, data, size);
        memory::pop_mem_profile();
    }
    finish_loading(data, size);
    return 0;
}

// 0x004093F0
int SceneAsset::read_in(const char* name, bool async, bool /*use_archive*/, void* params, void* structure)
{
    start_loading();
    set_up_params(params, structure);
    copy_string(args_->name.data(), name);
    std::array<char, 256> file{};
    build_scene_file_path(file.data(), name, core::at(args_->flags, 3));
    uint32_t size = 0;
    if (!read_file(this, file.data(), async, &size))
        return -1;
    start_reading(this, file_ref_, octets_, size, async);
    if (!async)
        finish_now(this, size);
    return 0;
}

// 0x00409240
bool SceneAsset::finish_loading(void* buffer, uint32_t size)
{
    void* textures = group_data(this);
    change_contents(
        make_scene(args_->name.data(), buffer, size, textures, core::at(args_->flags, 0), core::at(args_->flags, 1), core::at(args_->flags, 2), core::at(args_->flags, 3)));
    free_load_params(args_);
    args_ = nullptr;
    close_file(file_ref_);
    return Resource::finish_loading(buffer, size);
}

// 0x00409380
int SceneAsset::offload()
{
    if (get_data()) {
        unload_scene(get_data(), 0);
        free_malloc_buffer(octets_);
        change_contents(nullptr);
    }
    return Resource::offload();
}

// --- scene collision ------------------------------------------------------------------------------------------

// 0x00409750
const char* SceneCollisionAsset::fetch_title() const { return "SceneColl"; }

// 0x00409690
void SceneCollisionAsset::set_up_params(void* params, void* structure)
{
    auto made = memory::construct_in<SceneCollisionLoadParams>(alloc_load_params(sizeof(SceneCollisionLoadParams)));
    args_ = made; // (a record of the load-parameter pool, given back once loaded)
    args_->checksum = 0;
    args_->flag = 0;
    if (params)
        copy_words(args_, params, 0x12);
    if (auto* s = static_cast<Struct*>(structure)) {
        s->read_hash_value(0x26861025u, &args_->checksum, 0);
        args_->flag = s->has_bare_flag(0x417149C8u);
    }
}

// 0x00409890
int SceneCollisionAsset::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    memory::push_mem_profile("Collision processing");
    start_loading();
    set_up_params(params, structure);
    if (copy) {
        memory::push_mem_profile("SceneColl from data stream");
        data = copy_data(this, data, size);
        memory::pop_mem_profile();
    }
    finish_loading(data, size);
    memory::pop_mem_profile();
    return 0;
}

// 0x00409760
int SceneCollisionAsset::read_in(const char* name, bool async, bool /*use_archive*/, void* params, void* structure)
{
    start_loading();
    set_up_params(params, structure);
    copy_string(args_->name.data(), name);
    std::array<char, 256> file{};
    collision_file_name(file.data(), name, args_->flag);
    uint32_t size = 0;
    if (!read_file(this, file.data(), async, &size))
        return -1;
    start_reading(this, file_ref_, octets_, size, async);
    if (!async)
        finish_now(this, size);
    return 0;
}

// 0x00409610
bool SceneCollisionAsset::finish_loading(void* buffer, uint32_t size)
{
    void* scene = group_data(this);
    add_collision(scene, buffer, size, args_->flag); // (not original: the size)
    change_contents(scene_collision(scene));
    free_load_params(args_);
    args_ = nullptr;
    close_file(file_ref_);
    return Resource::finish_loading(buffer, size);
}

// 0x00409700
int SceneCollisionAsset::offload()
{
    if (get_data()) {
        detach_hit_mesh(group_data(this));
        free_malloc_buffer(octets_);
        change_contents(nullptr);
    }
    return Resource::offload();
}

// --- skeleton -------------------------------------------------------------------------------------------------

// 0x00409A40
int RigResource::fetch_kind() const { return kSkeleton; }
// 0x00409A30
const char* RigResource::fetch_title() const { return "Skeleton"; }

// 0x00409990 (the data isn't copied, whatever `copy` says)
int RigResource::load_from_buffer(void* data, uint32_t size, bool /*copy*/, void* /*params*/, void* /*structure*/)
{
    memory::push_mem_profile("Skeleton processing");
    start_loading();
    finish_loading(data, size);
    memory::pop_mem_profile();
    return 0;
}

// 0x00409A50
int RigResource::read_in(const char* name, bool async, bool /*use_archive*/, void* /*params*/, void* /*structure*/)
{
    start_loading();
    std::array<char, 256> file{};
    platform_name(name, file.data());
    file_ref_ = file::async_open(file.data(), !async, 100);
    if (!file_ref_)
        return -1;
    auto size = static_cast<uint32_t>(file_ref_->get_file_size());
    memory::enter_heap_scope(memory::top_down_heap());
    octets_ = memory::alloc_block(size);
    memory::leave_heap_scope();
    start_reading(this, file_ref_, octets_, size, async);
    if (!async)
        finish_now(this, size);
    return 0;
}

// 0x00409B80
bool RigResource::finish_loading(void* buffer, uint32_t size)
{
    void* skeleton = new gfx::RigData;
    if (!load_skeleton_data(skeleton, buffer, size))
        return false; // (the skeleton data leaks, as in the original)
    change_contents(skeleton);
    close_file(file_ref_);
    free_buffer();
    return Resource::finish_loading(buffer, size);
}

// 0x004099E0
int RigResource::offload()
{
    if (void* skeleton = get_data()) {
        gsl::owner<gfx::RigData*> rig = memory::adopt<gfx::RigData*>(skeleton);
        delete rig;
        change_contents(nullptr);
        free_buffer();
    }
    return Resource::offload();
}

// 0x00409A10
void RigResource::free_buffer() { free_malloc_buffer(octets_); }

// --- skinned model --------------------------------------------------------------------------------------------

// 0x00409E90
SkinParams* SkinParams::get_clear()
{
    flag0 = 0;
    value = 0;
    cas_data_ = nullptr;
    cas_size_ = 0; // (Not original: the original left +0x0C)
    has_cas_ = 0;
    on_heap_ = 0;
    return this;
}

// 0x00409E00
int SkinResource::fetch_kind() const { return kSkinnedModel; }
// 0x00409DF0
const char* SkinResource::fetch_title() const { return "Skin"; }

// 0x00409E10
void SkinResource::set_up_params(void* params, void* structure)
{
    args_ = static_cast<SkinParams*>(alloc_load_params(0x14));
    args_->get_clear(); // (inlined)
    if (params)
        copy_words(args_, params, 5);
    if (auto* s = static_cast<Struct*>(structure)) {
        s->fetch_int_value(0xBF34D221u, &args_->value, 0);
        args_->flag0 = s->has_bare_flag(0x01A1482Eu);
    }
}

// 0x0040A140
int SkinResource::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    start_loading();
    set_up_params(params, structure);
    if (copy && !args_->on_heap_) {
        memory::push_mem_profile("skin from data stream");
        data = copy_data(this, data, size);
        memory::pop_mem_profile();
    }
    finish_loading(data, size);
    return 0;
}

// 0x00409EB0
int SkinResource::read_in(const char* name, bool async, bool /*use_archive*/, void* params, void* structure)
{
    start_loading();
    set_up_params(params, structure);
    memory::push_mem_profile(name);
    std::array<char, 512> base{};
    const bool has_cas = split_skin_name(name, base.data());
    text::to_lower_text(base.data());
    args_->on_heap_ = 0;
    // (0x00645904 / 0x00645900: "skin" for a model with a .cas file, else "mdl")
    const char* kind = has_cas ? "skin" : "mdl";
    std::array<char, 512> file{};
    std::snprintf(file.data(), sizeof(file), "%s.%s.%s", base.data(), kind, render::platform_suffix());
    args_->has_cas_ = has_cas;
    ++files_reading_;
    uint32_t size = 0;
    if (has_cas && !async) {
        read_archive(this, &archive_, file.data(), &size, false);
    } else {
        if (!read_file(this, file.data(), async, &size))
            return -1;
        start_reading(this, file_ref_, octets_, size, async);
    }
    if (has_cas) {
        std::array<char, 512> cas{};
        std::snprintf(cas.data(), sizeof(cas), "%s.cas.%s", base.data(), render::platform_suffix());
        ++files_reading_;
        cas_file_ = file::async_open(cas.data(), !async, 100);
        if (!cas_file_)
            return -1;
        auto cas_size = static_cast<uint32_t>(cas_file_->get_file_size());
        cas_buffer_ = memory::alloc_block(cas_size);
        args_->cas_data_ = cas_buffer_;
        args_->cas_size_ = cas_size;
        start_reading(this, cas_file_, cas_buffer_, cas_size, async);
    }
    if (args_->on_heap_)
        memory::leave_heap_scope();
    if (!async) {
        if (cas_buffer_)
            --files_reading_;
        --files_reading_;
        finish_now(this, size);
    }
    memory::pop_mem_profile();
    return 0;
}

// 0x00409C70
bool SkinResource::finish_loading(void* buffer, uint32_t size)
{
    auto* textures = static_cast<render::TextureSet*>(group_data(this));
    const SkinParams* p = args_;
    gsl::owner<void*> model = make_skinned_model(checksum_, buffer, size, p->cas_data_, p->cas_size_, textures,
                                                 p->value, p->has_cas_, p->flag0);
    change_contents(model); // (the resource's: offload deletes it)
    if (args_->on_heap_ && octets_)
        free_buffer();
    free_load_params(args_);
    args_ = nullptr;
    close_file(file_ref_);
    close_file(cas_file_);
    free_malloc_buffer(cas_buffer_);
    return Resource::finish_loading(buffer, size);
}

// 0x00409D30
int SkinResource::offload()
{
    if (get_data()) {
        unload_skinned_model(get_data(), 1);
        change_contents(nullptr);
    }
    free_buffer();
    return Resource::offload();
}

// 0x00409D70
bool SkinResource::cancel_now()
{
    if (cas_file_) {
        cas_file_->get_cancel();
        file::async_close(cas_file_);
        cas_file_ = nullptr;
    }
    return Resource::cancel_now();
}

// 0x00409DA0
void SkinResource::free_buffer()
{
    free_malloc_buffer(cas_buffer_);
    if (archive_) {
        packs::offload(archive_);
        archive_ = 0;
        octets_ = nullptr;
        return;
    }
    if (octets_)
        memory::free_malloc(octets_);
    archive_ = 0;
    octets_ = nullptr;
}

// 0x00409C60
bool SkinResource::set_read()
{
    if (--files_reading_ != 0)
        return false;
    return Resource::set_read();
}

// --- skin collision -------------------------------------------------------------------------------------------

// 0x0040A290
int SkinCollisionAsset::fetch_kind() const { return kSkinCollision; }
// 0x0040A280
const char* SkinCollisionAsset::fetch_title() const { return "SkinColl"; }

namespace {
// The skin collision's parameters (0x44 bytes): from `params`, then the structure's checksum.
gsl::owner<SkinCollisionLoadParams*> skin_collision_params(void* params, void* structure)
{
    auto p = memory::construct_in<SkinCollisionLoadParams>(alloc_load_params(sizeof(SkinCollisionLoadParams)));
    p->checksum = 0;
    if (params)
        copy_words(p, params, 0x11);
    if (structure)
        static_cast<Struct*>(structure)->read_hash_value(0xFD8697E1u, &p->checksum, 0);
    return p;
}
} // namespace

// 0x0040A400
int SkinCollisionAsset::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    start_loading();
    gsl::owner<SkinCollisionLoadParams*> made = skin_collision_params(params, structure);
    args_ = made; // (a record of the load-parameter pool, given back once loaded)
    if (copy) {
        memory::push_mem_profile("SkinColl from data stream");
        data = copy_data(this, data, size);
        memory::pop_mem_profile();
    }
    finish_loading(data, size);
    return 0;
}

// 0x0040A2A0
int SkinCollisionAsset::read_in(const char* name, bool async, bool /*use_archive*/, void* params, void* structure)
{
    start_loading();
    gsl::owner<SkinCollisionLoadParams*> made = skin_collision_params(params, structure);
    args_ = made; // (a record of the load-parameter pool, given back once loaded)
    copy_string(args_->name.data(), name);
    std::array<char, 256> file{};
    skin_collision_file_name(file.data(), name);
    uint32_t size = 0;
    if (!read_file(this, file.data(), async, &size))
        return -1;
    start_reading(this, file_ref_, octets_, size, async);
    if (!async)
        finish_now(this, size);
    return 0;
}

// 0x0040A510
bool SkinCollisionAsset::finish_loading(void* buffer, uint32_t size)
{
    void* model = group_data(this);
    add_skin_collision(model, buffer, size); // (not original: the size)
    change_contents(static_cast<render::SubMesh*>(model)->hit_shape_); // (its collision objects)
    free_load_params(args_);
    args_ = nullptr;
    close_file(file_ref_);
    return Resource::finish_loading(buffer, size);
}

// 0x0040A230
int SkinCollisionAsset::offload()
{
    if (get_data()) {
        remove_skin_collision(group_data(this));
        free_malloc_buffer(octets_);
        change_contents(nullptr);
    }
    return Resource::offload();
}

// --- texture dictionary ---------------------------------------------------------------------------------------

// 0x0040A710
const char* TexDictAsset::fetch_title() const { return "TexDict"; }

// 0x0040A620
void TexDictAsset::set_up_params(void* params, void* structure)
{
    auto made = memory::construct_in<TexDictLoadParams>(alloc_load_params(sizeof(TexDictLoadParams)));
    args_ = made; // (a record of the load-parameter pool, given back once loaded)
    args_->flag = 1;
    args_->value = 0;
    args_->flag8 = 0;
    args_->flag9 = 0;
    if (params)
        copy_words(args_, params, 0x13);
    if (auto* s = static_cast<Struct*>(structure)) {
        s->fetch_int_value(0xBF34D221u, &args_->value, 0);
        args_->flag = s->has_bare_flag(0xE6237329u);
        args_->flag8 = s->has_bare_flag(0x6875B9C0u);
        args_->flag9 = s->has_bare_flag(0x86B52462u);
    }
}

// 0x0040A840
int TexDictAsset::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    start_loading();
    set_up_params(params, structure);
    if (copy) {
        memory::push_mem_profile("TexDict from data stream");
        data = copy_data(this, data, size);
        memory::pop_mem_profile();
    }
    finish_loading(data, size);
    return 0;
}

// 0x0040A720
int TexDictAsset::read_in(const char* name, bool async, bool /*use_archive*/, void* params, void* structure)
{
    start_loading();
    set_up_params(params, structure);
    copy_string(args_->name.data(), name);
    std::array<char, 256> file{};
    tex_dict_file_name(file.data(), name);
    uint32_t size = 0;
    if (!read_file(this, file.data(), async, &size))
        return -1;
    start_reading(this, file_ref_, octets_, size, async);
    if (!async)
        finish_now(this, size);
    return 0;
}

// 0x0040A5A0
bool TexDictAsset::finish_loading(void* buffer, uint32_t size)
{
    change_contents(
        make_tex_dict(checksum_, buffer, size, args_->flag, args_->value, args_->flag8, args_->flag9));
    free_load_params(args_);
    args_ = nullptr;
    close_file(file_ref_);
    return Resource::finish_loading(buffer, size);
}

// 0x0040A6C0
int TexDictAsset::offload()
{
    if (get_data()) {
        unload_tex_dict(get_data());
        free_malloc_buffer(octets_);
        change_contents(nullptr);
    }
    return Resource::offload();
}

} // namespace assets
