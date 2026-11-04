#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/memory/arena_alloc.h"
#include "engine/memory/mem.h"
#include "engine/core/checksums.h"
#include <gsl/pointers>

namespace script {
class Array;
}
namespace file {
class AsyncFileHandle;
}

// Assets: files the asset manager loads and keeps by checksum - scenes, textures, collision,
// animations, skeletons, skinned models, cutscenes, node arrays. Each type is a Resource with its own vtable; the
// manager (ResourceRegistry, resource_registry.h) makes them, lists them and frees them.
namespace assets {

// The asset types (each file extension maps to one, 0x0067C650).
enum class ResourceKind : uint8_t {
    kNone = 0, // (none given: the file name's extension says)
    kBinary = 1, // .BIN
    kScene = 2, // .SCN, .GEOM
    kResTexDict = 3, // .TEX
    kSceneCollision = 4, // .COL
    kAnimation = 5, // .SKA, .FAM
    kSkeleton = 6, // .SKE
    kSkinnedModel = 7, // .MDL, .SKIN
    kSkinCollision = 8,
    kCutscene = 9, // .CUT
    kNodeArray = 10, // .QB
    kOwnedNodeArray = 11, // a node array that the node manager's owner (made by LoadAsset with this type)
};

// The load states (+0x08).
enum class AssetState : uint8_t {
    kUnloaded = 0,
    kLoading = 1, // the file is being read
    kRead = 2, // the file is read (the async read's callback)
    kProcessing = 3,
    kLoaded = 4,
    kAborted = 5,
};

class Resource;
// Called when an asset the manager loaded asynchronously is ready: (the asset, its file's size, the two values).
using LoadCallback = void (*)(Resource* asset, uint32_t size, void* first_word, void* data1);

// What an asset is loaded with: the load's parameters and its structure.
struct LoadParams {
    void* params{};
    void* structure{};
};

// The base of every asset (0x4C bytes, vtable 0x00645568; each type has its own). The original's table also has
// GetData (slot 1, get_data here) and IsPermanent (slot 2: never called), change_contents, pin_resident,
// process_buffer and the state setters but set_read (slots 10-12, 16-21) - the same body in every table, so plain
// methods here.
class Resource : public memory::HeapObject {
public:
    // 0x00407910: unloaded, no file, buffer or callback, an empty dependents list.
    Resource();
    // 0x00407810 (0x00407950 / 0x00407AF0 the scalar deleting destructors, vtable slot 3: the base's / the types',
    // through 0x004093E0 - a jump to it): the dependents list's head unlinked.
    virtual ~Resource();
    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    // Vtable slot 0: the type (ResourceKind); the base's 0.
    [[nodiscard]] virtual ResourceKind fetch_kind() const { return ResourceKind::kNone; }
    // Slots 4 and 5: loaded from a buffer (data, size, copy, params, structure) / from a file (name, async,
    // use_archive, params, structure) - see the types below. 0x004075F0 (both, the base's): loading; 0.
    virtual int load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure);
    virtual int read_in(const char* name, bool async, bool use_archive, void* params, void* structure);
    // 0x00407600 (slot 6, the base's): unloaded; 0.
    virtual int offload();
    // Slot 7: unloaded and read again from `name`; whether that worked. The base's: false (a shared "return 0").
    virtual bool reload(const char* name);
    // 0x00407610 (slot 8)
    [[nodiscard]] virtual bool is_loaded() const;
    // 0x004076D0 (slot 9)
    [[nodiscard]] virtual const char* fetch_title() const;
    // 0x00407630 (slot 13, the base's): loaded; true.
    virtual bool finish_loading(void* buffer, uint32_t size);
    // 0x00407640 (slot 14): the file read cancelled and closed; aborted; true.
    virtual bool cancel_now();
    // Slot 15: the read buffer freed. The base's: nothing.
    virtual void free_buffer() {}
    // 0x00407680 (slot 17): read.
    virtual bool set_read();

    // Slot 1 (the same in every table - folded with ParkTile::fetch_serial): what was loaded.
    [[nodiscard]] void* get_data() const { return content_; }
    // 0x004075F0's body: the state set to loading (the types' loads start with it).
    void start_loading() { set_loading(); }
    // 0x00407620 (slot 12): finish_loading with the buffer and its size.
    bool process_buffer();
    // 0x004076E0 (slot 10), 0x004072F0 (slot 11), 0x004076F0, 0x00407700, 0x004075D0
    void change_contents(void* data);
    void pin_resident(bool permanent);
    void change_cluster(Resource* group);
    void set_checksum(uint32_t checksum);
    // The words a resource's hook is called with.
    struct HookWords {
        void* first_word_{};
        void* data1{};
    };

    void change_hook(LoadCallback callback, HookWords hook_words);
    // The other state setters (slots 16, 18-21): 0x00407670 (returns 1), 0x00407690, 0x004076A0, 0x004076B0,
    // 0x004076C0 (each returns true).
    int set_loading();
    bool set_processing();
    bool set_loaded();
    bool set_unloaded();
    bool set_aborted();

    // 0x00407840: `asset` onto the end of the dependents list (a failed allocation crashes, as in the original).
    void add_dependent(Resource* asset);
    // 0x00407890: the first dependents-list node of `asset` deleted.
    void remove_dependent(Resource* asset);

    // A read finished (the file system's completion hook's arguments, in order): the file, the event, the size read,
    // the asset and the other word given with it.
    struct ReadDone {
        file::AsyncFileHandle* file{};
        int event{};
        int size{};
        void* asset{};
        void* other{};
    };

    // 0x00407710: the async read's callback (the asset is the first value): its size kept, the state set to read
    // (unless the second value is set).
    static void read_callback(const ReadDone& done);

    // The fields other code reads and sets.
    [[nodiscard]] uint32_t checksum() const { return checksum_; }
    AssetState& cur_phase() { return cur_phase_; }
    [[nodiscard]] AssetState cur_phase() const { return cur_phase_; }
    void set_cur_phase(AssetState value) { cur_phase_ = value; }
    [[nodiscard]] bool pinned() const { return pinned_; }
    void set_pinned(bool value) { pinned_ = value; }
    LoadCallback& callback() { return callback_; }
    [[nodiscard]] const LoadCallback& callback() const { return callback_; }
    [[nodiscard]] void* callback_data0() const { return callback_data0_; }
    [[nodiscard]] void* callback_data1() const { return callback_data1_; }
    file::AsyncFileHandle*& file_ref() { return file_ref_; }
    [[nodiscard]] file::AsyncFileHandle* file_ref() const { return file_ref_; }
    void set_file_ref(file::AsyncFileHandle* value) { file_ref_ = value; }
    void*& octets() { return octets_; }
    [[nodiscard]] void* octets() const { return octets_; }
    void set_octets(void* value) { octets_ = value; }
    [[nodiscard]] uint32_t extent() const { return extent_; }
    void set_extent(uint32_t value) { extent_ = value; }
    [[nodiscard]] Resource* last_value() const { return last_value_; }
    void set_last_value(Resource* value) { last_value_ = value; }
    [[nodiscard]] Resource* succ() const { return succ_; }
    void set_succ(Resource* value) { succ_ = value; }
    [[nodiscard]] Resource* bundle() const { return bundle_; }

private:
    uint32_t checksum_{}; // +0x04
    AssetState cur_phase_{AssetState::kUnloaded}; // +0x08 (the original's int: the rest is zero)
    std::array<uint8_t, 3> unknown09_{};
    bool pinned_{};     // +0x0C not unloaded by unload_all_assets(false)
    std::array<uint8_t, 3> pad0d_{};
    void* content_{};                 // +0x10 what was loaded (a scene, a texture dictionary...)
    LoadCallback callback_{nullptr};  // +0x14
    void* callback_data0_{};          // +0x18
    void* callback_data1_{};          // +0x1C
    file::AsyncFileHandle* file_ref_{nullptr}; // +0x20 the file being read
    void* octets_{nullptr};                    // +0x24 its contents
    uint32_t extent_{0};                       // +0x28 their size
    Resource* last_value_{};          // +0x2C in the manager's list (circular)
    Resource* succ_{};                // +0x30
    Resource* bundle_{nullptr};       // +0x34 the asset it depends on (a model's textures)
    memory::ListNode dependents_{};   // +0x38 the assets depending on it (lists::Foremost<Resource>)
};

// The load parameters' pool: 4 blocks of 0x60 bytes (0x00691758, in use: the bits of 0x006918D8). 0x00407730: a
// free block (the size is ignored; null if all four are in use). 0x00407770: one given back.
void* alloc_load_params(uint32_t size);
void free_load_params(void* params);

// The types' own methods (the slots their vtables override, and their helpers). Each Load is (name, async,
// use_archive, params, structure) and load_from_buffer (data, size, copy, params, structure): `params` a copy of the
// type's parameters to start from (or null), `structure` a script structure to read them from (or null). Both
// return 0, or -1 if the file couldn't be opened.

// 0x00408A70: the types' reload (the animation's, the cutscene's, the node array's): unloaded, then read_in(name);
// whether that worked.
bool refresh_resource(Resource* asset, const char* name);

// Animation (+0x4C the checksum of the archive file it came from).
struct AnimResource final : Resource {
    // 0x004071A0 (vtable 0x006454B0)
    AnimResource();
private:
    uint32_t archive_{0};

public:
    // 0x004072E0 / 0x004072D0
    [[nodiscard]] ResourceKind fetch_kind() const override;
    [[nodiscard]] const char* fetch_title() const override;
    // 0x004071C0 / 0x00407300 (an archive file: through Pip, the lowercased name's checksum kept)
    int load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure) override;
    int read_in(const char* name, bool async, bool use_archive, void* params, void* structure) override;
    bool reload(const char* name) override { return refresh_resource(this, name); }
    // 0x004074F0: the frames made from the buffer (null if that fails).
    bool finish_loading(void* buffer, uint32_t size) override;
    // 0x004074A0: the frames deleted, the buffer freed.
    int offload() override;
    // 0x00407290 (also the node arrays'): the archive file unloaded, or the buffer freed.
    void free_buffer() override;
};

// Cutscene (the data a CinematicTable).
struct CutsceneResource final : Resource {
    // 0x00407A30 (vtable 0x00645600)
    CutsceneResource();
    // 0x00408AC0 / 0x00408AB0
    [[nodiscard]] ResourceKind fetch_kind() const override;
    [[nodiscard]] const char* fetch_title() const override;
    // 0x00408AD0: the cutscene data made and loaded from "<name>.<platform extension>" (the data leaks if that
    // fails, as in the original).
    int read_in(const char* name, bool async, bool use_archive, void* params, void* structure) override;
    // 0x00408A40: the cutscene data deleted.
    int offload() override;
    // 0x00408AA0: its cutscene data's.
    [[nodiscard]] bool is_loaded() const override;
    bool reload(const char* name) override { return refresh_resource(this, name); }
};

// Node array (a .qb file: its nodes, added to the node manager).
struct NodeTableResource final : Resource {
    // 0x00408BB0 (vtable 0x00645698)
    NodeTableResource();
    // The fields other code reads and sets.
    [[nodiscard]] script::Array* nodes() const { return nodes_; }

private:
    uint32_t archive_{0};    // +0x4C
    uint32_t source_path_{}; // +0x50 the checksum of the file name (its script's)
    uint32_t array_name_{};  // +0x54 the node array's checksum
    script::Array* nodes_{}; // +0x58 its script array
    char* args_{};           // +0x5C (0x40 bytes, the file name)
    bool owned_{};           // +0x60 the node manager's owner
    std::array<uint8_t, 3> pad61{};

public:
    // 0x00408BE0: ResourceKind::kNodeArray, or ResourceKind::kOwnedNodeArray when owned; 0x00408BD0
    [[nodiscard]] ResourceKind fetch_kind() const override;
    [[nodiscard]] const char* fetch_title() const override;
    // 0x00408EA0 / 0x00408D10 (the file read on the top-down heap)
    int load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure) override;
    int read_in(const char* name, bool async, bool use_archive, void* params, void* structure) override;
    bool reload(const char* name) override { return refresh_resource(this, name); }
    // 0x00408BF0: made the node manager's owner (owned_ set).
    void make_owner();
    // 0x00408C10: the script parsed, the node array added to the node manager.
    bool finish_loading(void* buffer, uint32_t size) override;
    // 0x00408F00: each node's object told (slot 6), the node array removed, the script unloaded.
    int offload() override;
    // 0x004090D0: a new array of `count` empty structures made the global symbol `name` (any old one removed)
    // and the node array's (returns it).
    void* build_node_table(checksums::Name table_name, int count);
};

constexpr size_t kSceneNameSize = 0x40;
constexpr size_t kSceneCollisionNameSize = 0x43;
constexpr size_t kSkinCollisionNameSize = 0x40;
constexpr size_t kTexDictNameSize = 0x42;
constexpr size_t kSceneLoadParamsBytes = 0x48; // the scene's and the scene collision's
constexpr size_t kSkinCollisionLoadParamsBytes = 0x44;
constexpr size_t kTexDictLoadParamsBytes = 0x4C;
// The asset types' load parameters (a pool block each; a caller's `params` are copied in whole).
struct SceneLoadParams {
    uint32_t textures;              // the textures' checksum
    std::array<uint8_t, 4> flags;   // (four flags: the scene maker's)
    std::array<char, kSceneNameSize> name;
};
struct SceneCollisionLoadParams {
    uint32_t checksum;
    uint8_t flag;
    std::array<char, kSceneCollisionNameSize> name;
};
struct SkinCollisionLoadParams {
    uint32_t checksum;
    std::array<char, kSkinCollisionNameSize> name;
};
struct TexDictLoadParams {
    uint8_t flag;                  // (set by default)
    std::array<uint8_t, 3> unknown01;
    int value;
    uint8_t flag8;
    uint8_t flag9;
    std::array<char, kTexDictNameSize> name;
};
static_assert(sizeof(SceneLoadParams) == kSceneLoadParamsBytes && sizeof(SceneCollisionLoadParams) == kSceneLoadParamsBytes &&
                  sizeof(SkinCollisionLoadParams) == kSkinCollisionLoadParamsBytes && sizeof(TexDictLoadParams) == kTexDictLoadParamsBytes,
              "the callers' parameter blocks (copied in whole)");

// Scene (+0x4C its load parameters: +0x00 the textures' checksum, +0x04-0x07 four flags, +0x08 the name).
struct SceneAsset final : Resource {
    // 0x00409220 (vtable 0x00645700)
    SceneAsset();
private:
    SceneLoadParams* args_{nullptr}; // (a record of the load-parameter pool while it loads)

public:
    // 0x004093D0
    // (0x0049B8C0, shared: a constant)
    [[nodiscard]] ResourceKind fetch_kind() const override { return ResourceKind::kScene; }
    [[nodiscard]] const char* fetch_title() const override;

    // 0x004092D0: the parameters from the pool (from `params`, then `structure`'s values).
    void set_up_params(LoadParams load_params);
    // 0x00409520 / 0x004093F0
    int load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure) override;
    int read_in(const char* name, bool async, bool use_archive, void* params, void* structure) override;
    // 0x00409240: the scene made, with its group's (the textures') data.
    bool finish_loading(void* buffer, uint32_t size) override;
    // 0x00409380
    int offload() override;
};

// Scene collision (+0x4C: +0x00 a checksum, +0x04 a flag, +0x05 the name).
struct SceneCollisionAsset final : Resource {
    // 0x004095F0 (vtable 0x00645778)
    SceneCollisionAsset();
private:
    SceneCollisionLoadParams* args_{nullptr}; // (a record of the load-parameter pool while it loads)

public:
    // 0x00409750
    // (0x0049BB70, shared: a constant)
    [[nodiscard]] ResourceKind fetch_kind() const override { return ResourceKind::kSceneCollision; }
    [[nodiscard]] const char* fetch_title() const override;
    // 0x00409690
    void set_up_params(LoadParams load_params);
    // 0x00409890 / 0x00409760
    int load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure) override;
    int read_in(const char* name, bool async, bool use_archive, void* params, void* structure) override;
    // 0x00409610: the collision added to its group's (the scene's) data.
    bool finish_loading(void* buffer, uint32_t size) override;
    // 0x00409700
    int offload() override;
};

// Skeleton.
struct RigResource final : Resource {
    // 0x00409970 (vtable 0x00645810)
    RigResource();
    // 0x00409A40 / 0x00409A30
    [[nodiscard]] ResourceKind fetch_kind() const override;
    [[nodiscard]] const char* fetch_title() const override;
    // 0x00409990 / 0x00409A50 (the buffer from the top-down heap)
    int load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure) override;
    int read_in(const char* name, bool async, bool use_archive, void* params, void* structure) override;
    // 0x00409B80: the skeleton data made (false and nothing kept if that fails).
    bool finish_loading(void* buffer, uint32_t size) override;
    // 0x004099E0 / 0x00409A10
    int offload() override;
    void free_buffer() override;
};

// The skinned model's load parameters (0x14 bytes from the pool): 0x00409E90 clears them (returns them).
struct SkinParams {
    uint8_t flag0; // +0x00
    std::array<uint8_t, 3> pad01;
    int value;       // +0x04
    void* cas_data_; // +0x08 the .cas file's contents
    uint32_t
        cas_size_;    // +0x0C (unused in the original) Not original - the .cas contents' size, set where they are read
    uint8_t has_cas_; // +0x10
    uint8_t on_heap_; // +0x11 (cleared by Load; a pop of the heap context when set)
    std::array<uint8_t, 2> pad12;
};

SkinParams* get_clear(SkinParams& params);

// Skinned model (+0x4C archive, +0x50 parameters, +0x54 / +0x58 the .cas file and its contents, +0x5C the files
// still being read).
struct SkinResource final : Resource {
    // 0x00409C40 (vtable 0x00645888)
    SkinResource();
private:
    uint32_t archive_{};
    SkinParams* args_{nullptr};
    file::AsyncFileHandle* cas_file_{nullptr};
    void* cas_buffer_{nullptr};
    int files_reading_{0};

public:
    // 0x00409E00 / 0x00409DF0
    [[nodiscard]] ResourceKind fetch_kind() const override;
    [[nodiscard]] const char* fetch_title() const override;
    // 0x00409E10
    void set_up_params(LoadParams load_params);
    // 0x0040A140 / 0x00409EB0 (the model file "<base>.skin|mdl.<ext>", and the base's .cas file if it has one)
    int load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure) override;
    int read_in(const char* name, bool async, bool use_archive, void* params, void* structure) override;
    // 0x00409C70: the model made (with its group's textures).
    bool finish_loading(void* buffer, uint32_t size) override;
    // 0x00409D30 / 0x00409D70 / 0x00409DA0
    int offload() override;
    bool cancel_now() override;
    void free_buffer() override;
    // 0x00409C60: read only when both files are.
    bool set_read() override;
};

// Skin collision (+0x4C: +0x00 a checksum, +0x04 the name).
struct SkinCollisionAsset final : Resource {
    // 0x0040A210 (vtable 0x00645928)
    SkinCollisionAsset();
private:
    SkinCollisionLoadParams* args_{nullptr}; // (a record of the load-parameter pool while it loads)

public:
    // 0x0040A290 / 0x0040A280
    [[nodiscard]] ResourceKind fetch_kind() const override;
    [[nodiscard]] const char* fetch_title() const override;
    // 0x0040A400 / 0x0040A2A0
    int load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure) override;
    int read_in(const char* name, bool async, bool use_archive, void* params, void* structure) override;
    // 0x0040A510: the collision added to its group's (the model's) data.
    bool finish_loading(void* buffer, uint32_t size) override;
    // 0x0040A230
    int offload() override;
};

// Texture dictionary (+0x4C: +0x00 a flag (set), +0x04 a value, +0x08 / +0x09 flags, +0x0A the name).
struct TexDictAsset final : Resource {
    // 0x0040A580 (vtable 0x006459A8)
    TexDictAsset();
private:
    TexDictLoadParams* args_{nullptr}; // (a record of the load-parameter pool while it loads)

public:
    // 0x0040A710
    // (0x0049C0F0, shared: a constant)
    [[nodiscard]] ResourceKind fetch_kind() const override { return ResourceKind::kResTexDict; }
    [[nodiscard]] const char* fetch_title() const override;
    // 0x0040A620
    void set_up_params(LoadParams load_params);
    // 0x0040A840 / 0x0040A720
    int load_from_buffer(void* data, uint32_t size, bool copy, void* params, void* structure) override;
    int read_in(const char* name, bool async, bool use_archive, void* params, void* structure) override;
    // 0x0040A5A0
    bool finish_loading(void* buffer, uint32_t size) override;
    // 0x0040A6C0
    int offload() override;
};

} // namespace assets
