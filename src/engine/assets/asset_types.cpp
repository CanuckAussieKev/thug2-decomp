// The asset types: animations, cutscenes, node arrays, scenes, scene collision, skeletons, skinned models, skin
// collision and texture dictionaries - how each is read and turned into the engine's data.

#include <cstddef>
#include <cstdint>
#include <span>
#include <cstdio>
#include <cstring>
#include <array>

#include "engine/assets/resource_entry.h"
#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/layout_32bit.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/core/str.h"
#include "engine/file/async.h"
#include "engine/file/packs.h"
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_mesh.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/engine_glue.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/entity.h"
#include "engine/script/cell.h"
#include "engine/script/node_manager.h"
#include "engine/script/qb.h"
#include "engine/script/script_startup.h"
#include "engine/script/param_struct.h"
#include <gsl/pointers>
#include <gsl/assert>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/core/gathered_call.h"

namespace assets {

namespace {
// Sizes: the pool of load parameter blocks, the paths and names built while loading.
constexpr size_t kParamBlockCount = 4;
constexpr int kParamBlockBytes = 0x60;
constexpr size_t kParamsPoolBytes = kParamBlockCount * kParamBlockBytes;
constexpr size_t kPathSize = 256;
constexpr size_t kSkinNameSize = 512;
constexpr size_t kArrayNameSize = 128;
constexpr int kAnimFramesBytes = 0x3C; // (the original's allocation: sizeof on x64)
static_assert(core::layout_32bit(sizeof(gfx::JointClipFrames) == kAnimFramesBytes), "the original allocates this many bytes");
constexpr int kNodeTableParamBytes = 0x40;
constexpr int kNodeTableParamWords = 0x10;
constexpr int kSceneParamWords = 0x12;
constexpr int kSceneCollisionParamWords = 0x12;
constexpr int kSkinParamBytes = 0x14;
constexpr int kSkinParamWords = 5;
constexpr int kSkinCollisionParamWords = 0x11;
constexpr int kTexDictParamWords = 0x13;

// Checksums (the text each hashes; "text unknown" where the name isn't known).
constexpr uint32_t kAssertOnMissingAssets = 0x25DC7904U; // AssertOnMissingAssets
constexpr uint32_t kName = 0xA1DC81F9U;                  // name
constexpr uint32_t kTexDict = 0xDC8116B2U;               // tex_dict
constexpr uint32_t kAddSuperSectors = 0x9E6EA452U;       // add_super_sectors
constexpr uint32_t kIsSky = 0xB5BD28D8U;                 // is_sky
constexpr uint32_t kIsDictionary = 0x3D559E7DU;          // is_dictionary
constexpr uint32_t kIsNet = 0x417149C8U;                 // is_net
constexpr uint32_t kSceneKey = 0x26861025U;              // scene
constexpr uint32_t kTexDictOffset = 0xBF34D221U;         // tex_dict_offset
constexpr uint32_t kSkinFlag0 = 0x01A1482EU;             // text unknown: the skin's first flag
constexpr uint32_t kSkin = 0xFD8697E1U;                  // skin
constexpr uint32_t kTexDictFlag = 0xE6237329U;           // text unknown: the texture dictionary's flag
constexpr uint32_t kTexDictFlag8 = 0x6875B9C0U;          // text unknown: its flag8
constexpr uint32_t kTexDictFlag9 = 0x86B52462U;          // text unknown: its flag9
// NOT ORIGINAL grouping - the asset loads' parameter blocks and how many are in use. Made on first use, never destroyed
// (params_state()).
struct ParamsPool {
    uint32_t params_in_use_{};
    // --- shared steps ---------------------------------------------------------------------------------------------
    std::array<uint8_t, kParamsPoolBytes> params_pool_{}; // 4 blocks of 0x60 bytes
};

ParamsPool& params_state()
{
    static core::NoDestroy<ParamsPool> state;
    return state.get();
}

using script::Struct;

// --- the engine's loaders ---------------------------------------------------------------------------------
// gfx::JointClipFrames (0x3C bytes): 0x00482AF0 the constructor, 0x00482B30 the destructor, 0x004856C0 Load.
gsl::owner<void*> construct_anim_frames(void* frames) { return new (frames) gfx::JointClipFrames; }
void destroy_anim_frames(void* frames) { static_cast<gfx::JointClipFrames*>(frames)->~JointClipFrames(); }
bool load_anim_frames(void* frames, void* buffer, uint32_t size)
{
    Expects(frames != nullptr);
    return static_cast<gfx::JointClipFrames*>(frames)->read_in(static_cast<const uint8_t*>(buffer), size, true);
}
// The skeleton data (0x598 bytes): 0x004A2C20 the constructor, 0x004A39C0 Load.

bool load_skeleton_data(void* skeleton, void* buffer, uint32_t size)
{
    return static_cast<gfx::RigData*>(skeleton)->read_in(static_cast<const uint32_t*>(buffer), size, true);
}
// Scripts: 0x005A5990 parses a .qb file's contents, 0x005A59B0 unloads one (by its name's checksum).
void parse_qb_file(const char* name, void* buffer, uint32_t size)
{
    script::load_qb_file_data(name, std::span<uint8_t>(static_cast<uint8_t*>(buffer), size), 1, 1);
}
void unload_qb_file(uint32_t name) { script::unload_qb_file(name); }
// Scenes (Nx): the file name, made from the buffer (with the textures), unloaded.
void build_scene_file_path(char* out, const char* name, uint8_t flag)
{
    render::engine::build_scene_file_path(out, name, flag);
}
void* make_scene(const char* name, void* buffer, uint32_t size, void* textures, uint8_t a, uint8_t b, uint8_t flag_c,
                 uint8_t flag_d)
{
    return render::load_named_scene_from_memory(name, static_cast<const uint8_t*>(buffer), size,
                                                static_cast<render::TextureSet*>(textures), a != 0, b != 0, flag_c != 0, flag_d);
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
bool split_skin_name(const char* name, std::span<char> base) { return render::engine::split_skin_name(name, base); }
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
void* make_tex_dict(uint32_t checksum, void* buffer, uint32_t size, uint8_t a, int value, uint8_t b, uint8_t flag_c)
{
    return render::load_tex_dict_from_memory(checksum, static_cast<const uint8_t*>(buffer), size, a,
                                             static_cast<uint32_t>(value), b, flag_c);
}
void unload_tex_dict(void* textures) { render::unload_tex_dict(static_cast<render::TextureSet*>(textures)); }

std::array<uint8_t, kParamsPoolBytes>& params_pool() { return params_state().params_pool_; }
uint32_t& params_in_use() { return params_state().params_in_use_; }

// strcpy (no bounds, as the original's).
void copy_string(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
// The file opened (null: false) and read into a new buffer; an async read calls back when it's done.
bool read_file(Resource* asset, const char* file, bool async, uint32_t* size)
{
    asset->set_file_ref(file::async_open(file, !async, file::kDefaultReadRank));
    if (!asset->file_ref()) {
        return false;
    }
    *size = static_cast<uint32_t>(asset->file_ref()->get_file_size());
    asset->set_octets(memory::alloc_block(*size));
    return true;
}
void start_reading(Resource* asset, file::AsyncFileHandle* file, void* buffer, uint32_t size, bool async)
{
    if (async) {
        file->callback_mask() = 0;
        file->callback_mask() |= 1U << file::kOpReadFile;
        file->change_hook(&core::gathered<Resource::ReadDone, &Resource::read_callback>, asset, nullptr);
    }
    file->get_take_in(buffer, 1, static_cast<int>(size));
}
// An archive file through Pip: its lowercased name's checksum kept (`lowered` false: the name is already).
void read_archive(Resource* asset, uint32_t* archive, const char* file, uint32_t* size, bool lower)
{
    std::array<char, kPathSize> lowered{};
    const char* key = file;
    if (lower) {
        copy_string(lowered.data(), file);
        text::to_lower_text(lowered.data());
        key = lowered.data();
    }
    *archive = checksums::hash_from_text(key);
    asset->set_octets(packs::read_in(file, false, nullptr, nullptr, nullptr));
    *size = static_cast<uint32_t>(packs::get_file_size(file));
}
// A blocking load finishes at once.
void finish_now(Resource* asset, uint32_t size)
{
    asset->set_extent(size);
    asset->process_buffer();
}
// load_from_buffer's copy: the data copied into a buffer of the asset's own.
void* copy_data(Resource* asset, void* data, uint32_t size)
{
    asset->set_octets(memory::alloc_block(size));
    std::memcpy(asset->octets(), data, size);
    return asset->octets();
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
void* group_data(Resource* asset) { return asset->bundle()->get_data(); }
const char* platform_name(const char* name, char* out)
{
    text::format_to(std::span(out, kPathSize), "%s.%s", name, render::platform_suffix());
    return out;
}
void copy_words(void* to, const void* from, int words) { std::memcpy(to, from, 4 * static_cast<size_t>(words)); }

} // namespace

// 0x00407730
void* alloc_load_params(uint32_t /*size*/)
{
    for (int i = 0; i < 4; ++i) {
        if ((params_in_use() & (1U << i)) == 0U) {
            params_in_use() |= 1U << i;
            return std::span(params_pool()).subspan(kParamBlockBytes * static_cast<size_t>(i)).data();
        }
    }
    return nullptr;
}

// 0x00407770
void free_load_params(void* params)
{
    for (int i = 0; i < 4; ++i) {
        if (std::span(params_pool()).subspan(kParamBlockBytes * static_cast<size_t>(i)).data() == params) {
            params_in_use() &= ~(1U << i);
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
ResourceKind AnimResource::fetch_kind() const { return ResourceKind::kAnimation; }
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
    script::fetch_int_value(kAssertOnMissingAssets); // (a debug leftover: looked up and not used)
    memory::push_mem_profile(name);
    std::array<char, kPathSize> file{};
    platform_name(name, file.data());
    uint32_t size = 0;
    if (use_archive) {
        read_archive(this, &archive_, file.data(), &size, true);
    } else {
        if (!read_file(this, file.data(), async, &size)) {
            return -1;
        }
        start_reading(this, file_ref(), octets(), size, async);
    }
    if (!async) {
        finish_now(this, size); // (the original reads the checksum first and doesn't use it)
    }
    memory::pop_mem_profile();
    return 0;
}

// 0x004074F0
bool AnimResource::finish_loading(void* buffer, uint32_t size)
{
    void* memory = memory::alloc(sizeof(gfx::JointClipFrames));
    void* frames = memory ? construct_anim_frames(memory) : nullptr;
    if (!load_anim_frames(frames, buffer, size)) { // (called on a failed allocation too, as in the original)
        script::fetch_int_value(kAssertOnMissingAssets);
        if (frames) {
            destroy_anim_frames(frames);
            memory::Hub::dealloc(frames);
        }
        frames = nullptr;
    }
    change_contents(frames);
    close_file(file_ref());
    return Resource::finish_loading(buffer, size);
}

// 0x004074A0
int AnimResource::offload()
{
    if (get_data()) {
        if (void* frames = get_data()) {
            destroy_anim_frames(frames);
            memory::Hub::dealloc(frames);
        }
        change_contents(nullptr);
    }
    free_buffer();
    return Resource::offload();
}

// 0x00407290
void AnimResource::free_buffer()
{
    if (archive_ != 0U) {
        packs::offload(archive_);
        archive_ = 0;
        set_octets(nullptr);
        return;
    }
    if (octets()) {
        memory::free_malloc(octets());
    }
    archive_ = 0;
    set_octets(nullptr);
}

// --- cutscene -------------------------------------------------------------------------------------------------

// 0x00408AC0
ResourceKind CutsceneResource::fetch_kind() const { return ResourceKind::kCutscene; }
// 0x00408AB0
const char* CutsceneResource::fetch_title() const { return "Cutscene"; }

// 0x00408AD0 (no start_loading: the state stays as it was)
int CutsceneResource::read_in(const char* name, bool async, bool /*use_archive*/, void* params, void* /*structure*/)
{
    memory::push_mem_profile(name);
    gsl::owner<objects::CinematicTable*> cutscene{memory::make_owned<objects::CinematicTable>()};
    std::array<char, kPathSize> file{};
    platform_name(name, file.data());
    if (!cutscene->read_in(file.data(), 1, static_cast<int>(async),
                           static_cast<const objects::CutsceneSkater*>(params))) {
        return -1;
    }
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
ResourceKind NodeTableResource::fetch_kind() const { return owned_ ? ResourceKind::kOwnedNodeArray : ResourceKind::kNodeArray; }
// 0x00408BD0
const char* NodeTableResource::fetch_title() const { return "Node Array"; }

// 0x00408EA0
int NodeTableResource::load_from_buffer(void* data, uint32_t size, bool /*copy*/, void* params, void* /*structure*/)
{
    start_loading();
    args_ = static_cast<char*>(alloc_load_params(kNodeTableParamBytes));
    if (params) {
        copy_words(args_, params, kNodeTableParamWords);
    }
    finish_loading(data, size);
    return 0;
}

// 0x00408D10
int NodeTableResource::read_in(const char* name, bool async, bool use_archive, void* params, void* /*structure*/)
{
    start_loading();
    args_ = static_cast<char*>(alloc_load_params(kNodeTableParamBytes));
    if (params) {
        copy_words(args_, params, kNodeTableParamWords);
    }
    copy_string(args_, name);
    memory::enter_heap_scope(memory::top_down_heap());
    uint32_t size = 0;
    if (use_archive) {
        read_archive(this, &archive_, name, &size, true);
    } else {
        if (!read_file(this, name, async, &size)) {
            return -1; // (with the heap context still pushed, as in the original)
        }
        start_reading(this, file_ref(), octets(), size, async);
    }
    if (!async) {
        finish_now(this, size);
    }
    memory::leave_heap_scope();
    return 0;
}

// 0x00408C10
bool NodeTableResource::finish_loading(void* buffer, uint32_t size)
{
    source_path_ = checksums::hash_from_text(args_);
    std::array<char, kArrayNameSize> array_name{};
    level_nodes::make_node_array_name(args_, array_name);
    parse_qb_file(args_, buffer, size);
    level_nodes::node_manager_instance()->add_node_array(level_nodes::node_array_checksum(args_), owned_);
    script::free_script_checksum_names();
    script::Array* nodes = script::fetch_list(array_name.data());
    array_name_ = checksums::hash_from_text(array_name.data());
    change_contents(nodes);
    nodes_ = static_cast<script::Array*>(get_data());
    close_file(file_ref());
    free_buffer();
    free_load_params(args_);
    args_ = nullptr;
    return Resource::finish_loading(buffer, size);
}

// 0x00408F00
int NodeTableResource::offload()
{
    auto* nodes = nodes_;
    for (uint32_t i = 0; i < nodes->extent(); ++i) {
        uint32_t id = 0;
        nodes->struct_at(i)->read_hash_value(kName, &id, 0); // name
        if (objects::Entity* object = objects::find_object(id)) {
            object->flag_as_dead();
        }
        nodes = nodes_;
    }
    if (level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get()) {
        manager->remove_node_array(array_name_);
    }
    if (get_data()) {
        unload_qb_file(source_path_);
    }
    free_buffer();
    return Resource::offload();
}

// 0x004090D0
void* NodeTableResource::build_node_table(checksums::Name table_name, int count)
{
    const uint32_t name = table_name.value();
    gsl::owner<script::Array*> array = script::alloc_script_array();
    if (array) {
        new (array) script::Array;
    }
    Expects(array != nullptr);
    array->configure_size_kind(count, script::QbValueKind::kStruct);
    for (int i = 0; i < count; ++i) {
        gsl::owner<Struct*> node = script::spawn_param_bag();
        if (node) {
            new (node) script::Struct;
        }
        array->set_cell(i, script::Cell::of(node));
    }
    if (script::SymbolTableEntry* old = script::find_symbol(name)) {
        script::clean_up_and_remove_symbol(old);
    }
    script::SymbolTableEntry* symbol = script::create_symbol(name);
    symbol->value().set_array(array);
    symbol->set_type(script::QbValueKind::kArray);
    array_name_ = name;
    script::Array* const table = array; // (the symbol's now)
    nodes_ = table;
    return table;
}

// --- scene ----------------------------------------------------------------------------------------------------

// 0x004093D0
const char* SceneAsset::fetch_title() const { return "Scene"; }

// 0x004092D0
void SceneAsset::set_up_params(LoadParams load_params)
{
    auto* const params = load_params.params;
    auto* const structure = load_params.structure;
    gsl::owner<assets::SceneLoadParams*> made{
        memory::construct_in<SceneLoadParams>(alloc_load_params(sizeof(SceneLoadParams)))};
    args_ = made; // (a record of the load-parameter pool, given back once loaded)
    args_->textures = 0;
    args_->flags = {{1, 0, 0, 0}};
    if (params) {
        copy_words(args_, params, kSceneParamWords);
    }
    if (auto* fields = static_cast<Struct*>(structure)) {
        fields->read_hash_value(kTexDict, &args_->textures, 0);
        args_->flags = {{static_cast<uint8_t>(fields->has_bare_flag(kAddSuperSectors)), static_cast<uint8_t>(fields->has_bare_flag(kIsSky)),
                         static_cast<uint8_t>(fields->has_bare_flag(kIsDictionary)), static_cast<uint8_t>(fields->has_bare_flag(kIsNet))}};
    }
}

// 0x00409520
int SceneAsset::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    start_loading();
    set_up_params({.params = params, .structure = structure});
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
    set_up_params({.params = params, .structure = structure});
    copy_string(args_->name.data(), name);
    std::array<char, kPathSize> file{};
    build_scene_file_path(file.data(), name, core::at(args_->flags, 3));
    uint32_t size = 0;
    if (!read_file(this, file.data(), async, &size)) {
        return -1;
    }
    start_reading(this, file_ref(), octets(), size, async);
    if (!async) {
        finish_now(this, size);
    }
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
    close_file(file_ref());
    return Resource::finish_loading(buffer, size);
}

// 0x00409380
int SceneAsset::offload()
{
    if (get_data()) {
        unload_scene(get_data(), 0);
        free_malloc_buffer(octets());
        change_contents(nullptr);
    }
    return Resource::offload();
}

// --- scene collision ------------------------------------------------------------------------------------------

// 0x00409750
const char* SceneCollisionAsset::fetch_title() const { return "SceneColl"; }

// 0x00409690
void SceneCollisionAsset::set_up_params(LoadParams load_params)
{
    const void* const params = load_params.params;
    void* const structure = load_params.structure;
    gsl::owner<assets::SceneCollisionLoadParams*> made{
        memory::construct_in<SceneCollisionLoadParams>(alloc_load_params(sizeof(SceneCollisionLoadParams)))};
    args_ = made; // (a record of the load-parameter pool, given back once loaded)
    args_->checksum = 0;
    args_->flag = 0;
    if (params) {
        copy_words(args_, params, kSceneCollisionParamWords);
    }
    if (auto* fields = static_cast<Struct*>(structure)) {
        fields->read_hash_value(kSceneKey, &args_->checksum, 0);
        args_->flag = static_cast<uint8_t>(fields->has_bare_flag(kIsNet));
    }
}

// 0x00409890
int SceneCollisionAsset::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    memory::push_mem_profile("Collision processing");
    start_loading();
    set_up_params({.params = params, .structure = structure});
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
    set_up_params({.params = params, .structure = structure});
    copy_string(args_->name.data(), name);
    std::array<char, kPathSize> file{};
    collision_file_name(file.data(), name, args_->flag);
    uint32_t size = 0;
    if (!read_file(this, file.data(), async, &size)) {
        return -1;
    }
    start_reading(this, file_ref(), octets(), size, async);
    if (!async) {
        finish_now(this, size);
    }
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
    close_file(file_ref());
    return Resource::finish_loading(buffer, size);
}

// 0x00409700
int SceneCollisionAsset::offload()
{
    if (get_data()) {
        detach_hit_mesh(group_data(this));
        free_malloc_buffer(octets());
        change_contents(nullptr);
    }
    return Resource::offload();
}

// --- skeleton -------------------------------------------------------------------------------------------------

// 0x00409A40
ResourceKind RigResource::fetch_kind() const { return ResourceKind::kSkeleton; }
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
    std::array<char, kPathSize> file{};
    platform_name(name, file.data());
    set_file_ref(file::async_open(file.data(), !async, file::kDefaultReadRank));
    if (!file_ref()) {
        return -1;
    }
    auto size = static_cast<uint32_t>(file_ref()->get_file_size());
    memory::enter_heap_scope(memory::top_down_heap());
    set_octets(memory::alloc_block(size));
    memory::leave_heap_scope();
    start_reading(this, file_ref(), octets(), size, async);
    if (!async) {
        finish_now(this, size);
    }
    return 0;
}

// 0x00409B80
bool RigResource::finish_loading(void* buffer, uint32_t size)
{
    void* skeleton = new gfx::RigData;
    if (!load_skeleton_data(skeleton, buffer, size)) {
        return false; // (the skeleton data leaks, as in the original)
    }
    change_contents(skeleton);
    close_file(file_ref());
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
void RigResource::free_buffer() { free_malloc_buffer(octets()); }

// --- skinned model --------------------------------------------------------------------------------------------

// 0x00409E90
SkinParams* get_clear(SkinParams& params)
{
    params.flag0 = 0;
    params.value = 0;
    params.cas_data_ = nullptr;
    params.cas_size_ = 0; // (Not original: the original left +0x0C)
    params.has_cas_ = 0;
    params.on_heap_ = 0;
    return &params;
}

// 0x00409E00
ResourceKind SkinResource::fetch_kind() const { return ResourceKind::kSkinnedModel; }
// 0x00409DF0
const char* SkinResource::fetch_title() const { return "Skin"; }

// 0x00409E10
void SkinResource::set_up_params(LoadParams load_params)
{
    const void* const params = load_params.params;
    void* const structure = load_params.structure;
    args_ = static_cast<SkinParams*>(alloc_load_params(kSkinParamBytes));
    get_clear(*args_); // (inlined)
    if (params) {
        copy_words(args_, params, kSkinParamWords);
    }
    if (auto* fields = static_cast<Struct*>(structure)) {
        fields->fetch_int_value(kTexDictOffset, &args_->value, 0);
        args_->flag0 = static_cast<uint8_t>(fields->has_bare_flag(kSkinFlag0));
    }
}

// 0x0040A140
int SkinResource::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    start_loading();
    set_up_params({.params = params, .structure = structure});
    if (copy && (args_->on_heap_ == 0U)) {
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
    set_up_params({.params = params, .structure = structure});
    memory::push_mem_profile(name);
    std::array<char, kSkinNameSize> base{};
    const bool has_cas = split_skin_name(name, base);
    text::to_lower_text(base.data());
    args_->on_heap_ = 0;
    // (0x00645904 / 0x00645900: "skin" for a model with a .cas file, else "mdl")
    const char* kind = has_cas ? "skin" : "mdl";
    std::array<char, kSkinNameSize> file{};
    text::format_to(file, "%s.%s.%s", base.data(), kind, render::platform_suffix());
    args_->has_cas_ = static_cast<uint8_t>(has_cas);
    ++files_reading_;
    uint32_t size = 0;
    if (has_cas && !async) {
        read_archive(this, &archive_, file.data(), &size, false);
    } else {
        if (!read_file(this, file.data(), async, &size)) {
            return -1;
        }
        start_reading(this, file_ref(), octets(), size, async);
    }
    if (has_cas) {
        std::array<char, kSkinNameSize> cas{};
        text::format_to(cas, "%s.cas.%s", base.data(), render::platform_suffix());
        ++files_reading_;
        cas_file_ = file::async_open(cas.data(), !async, file::kDefaultReadRank);
        if (!cas_file_) {
            return -1;
        }
        auto cas_size = static_cast<uint32_t>(cas_file_->get_file_size());
        cas_buffer_ = memory::alloc_block(cas_size);
        args_->cas_data_ = cas_buffer_;
        args_->cas_size_ = cas_size;
        start_reading(this, cas_file_, cas_buffer_, cas_size, async);
    }
    if (args_->on_heap_ != 0U) {
        memory::leave_heap_scope();
    }
    if (!async) {
        if (cas_buffer_) {
            --files_reading_;
        }
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
    const SkinParams* skin = args_;
    gsl::owner<void*> model = make_skinned_model(checksum(), buffer, size, skin->cas_data_, skin->cas_size_, textures,
                                                 skin->value, skin->has_cas_, skin->flag0);
    change_contents(model); // (the resource's: offload deletes it)
    if ((args_->on_heap_ != 0U) && octets()) {
        free_buffer();
    }
    free_load_params(args_);
    args_ = nullptr;
    close_file(file_ref());
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
    if (archive_ != 0U) {
        packs::offload(archive_);
        archive_ = 0;
        set_octets(nullptr);
        return;
    }
    if (octets()) {
        memory::free_malloc(octets());
    }
    archive_ = 0;
    set_octets(nullptr);
}

// 0x00409C60
bool SkinResource::set_read()
{
    if (--files_reading_ != 0) {
        return false;
    }
    return Resource::set_read();
}

// --- skin collision -------------------------------------------------------------------------------------------

// 0x0040A290
ResourceKind SkinCollisionAsset::fetch_kind() const { return ResourceKind::kSkinCollision; }
// 0x0040A280
const char* SkinCollisionAsset::fetch_title() const { return "SkinColl"; }

namespace {
// The skin collision's parameters (0x44 bytes): from `params`, then the structure's checksum.
gsl::owner<SkinCollisionLoadParams*> skin_collision_params(LoadParams load_params)
{
    auto* const params = load_params.params;
    auto* const structure = load_params.structure;
    gsl::owner<assets::SkinCollisionLoadParams*> collision_params{
        memory::construct_in<SkinCollisionLoadParams>(alloc_load_params(sizeof(SkinCollisionLoadParams)))};
    collision_params->checksum = 0;
    if (params) {
        copy_words(collision_params, params, kSkinCollisionParamWords);
    }
    if (structure) {
        static_cast<Struct*>(structure)->read_hash_value(kSkin, &collision_params->checksum, 0);
    }
    return collision_params;
}
} // namespace

// 0x0040A400
int SkinCollisionAsset::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    start_loading();
    gsl::owner<SkinCollisionLoadParams*> made = skin_collision_params({.params = params, .structure = structure});
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
    gsl::owner<SkinCollisionLoadParams*> made = skin_collision_params({.params = params, .structure = structure});
    args_ = made; // (a record of the load-parameter pool, given back once loaded)
    copy_string(args_->name.data(), name);
    std::array<char, kPathSize> file{};
    skin_collision_file_name(file.data(), name);
    uint32_t size = 0;
    if (!read_file(this, file.data(), async, &size)) {
        return -1;
    }
    start_reading(this, file_ref(), octets(), size, async);
    if (!async) {
        finish_now(this, size);
    }
    return 0;
}

// 0x0040A510
bool SkinCollisionAsset::finish_loading(void* buffer, uint32_t size)
{
    void* model = group_data(this);
    add_skin_collision(model, buffer, size); // (not original: the size)
    change_contents(static_cast<render::SubMesh*>(model)->hit_shape()); // (its collision objects)
    free_load_params(args_);
    args_ = nullptr;
    close_file(file_ref());
    return Resource::finish_loading(buffer, size);
}

// 0x0040A230
int SkinCollisionAsset::offload()
{
    if (get_data()) {
        remove_skin_collision(group_data(this));
        free_malloc_buffer(octets());
        change_contents(nullptr);
    }
    return Resource::offload();
}

// --- texture dictionary ---------------------------------------------------------------------------------------

// 0x0040A710
const char* TexDictAsset::fetch_title() const { return "TexDict"; }

// 0x0040A620
void TexDictAsset::set_up_params(LoadParams load_params)
{
    const void* const params = load_params.params;
    void* const structure = load_params.structure;
    gsl::owner<assets::TexDictLoadParams*> made{
        memory::construct_in<TexDictLoadParams>(alloc_load_params(sizeof(TexDictLoadParams)))};
    args_ = made; // (a record of the load-parameter pool, given back once loaded)
    args_->flag = 1;
    args_->value = 0;
    args_->flag8 = 0;
    args_->flag9 = 0;
    if (params) {
        copy_words(args_, params, kTexDictParamWords);
    }
    if (auto* fields = static_cast<Struct*>(structure)) {
        fields->fetch_int_value(kTexDictOffset, &args_->value, 0);
        args_->flag = static_cast<uint8_t>(fields->has_bare_flag(kTexDictFlag));
        args_->flag8 = static_cast<uint8_t>(fields->has_bare_flag(kTexDictFlag8));
        args_->flag9 = static_cast<uint8_t>(fields->has_bare_flag(kTexDictFlag9));
    }
}

// 0x0040A840
int TexDictAsset::load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure)
{
    start_loading();
    set_up_params({.params = params, .structure = structure});
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
    set_up_params({.params = params, .structure = structure});
    copy_string(args_->name.data(), name);
    std::array<char, kPathSize> file{};
    tex_dict_file_name(file.data(), name);
    uint32_t size = 0;
    if (!read_file(this, file.data(), async, &size)) {
        return -1;
    }
    start_reading(this, file_ref(), octets(), size, async);
    if (!async) {
        finish_now(this, size);
    }
    return 0;
}

// 0x0040A5A0
bool TexDictAsset::finish_loading(void* buffer, uint32_t size)
{
    change_contents(
        make_tex_dict(checksum(), buffer, size, args_->flag, args_->value, args_->flag8, args_->flag9));
    free_load_params(args_);
    args_ = nullptr;
    close_file(file_ref());
    return Resource::finish_loading(buffer, size);
}

// 0x0040A6C0
int TexDictAsset::offload()
{
    if (get_data()) {
        unload_tex_dict(get_data());
        free_malloc_buffer(octets());
        change_contents(nullptr);
    }
    return Resource::offload();
}

} // namespace assets
