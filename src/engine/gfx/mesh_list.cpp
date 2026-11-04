// A scene's mesh list and materials: see mesh_list.h.

#include "engine/gfx/mesh_list.h"

#include <windows.h>

#include <unknwn.h> // IUnknown

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/lists/key_map.h"
#include "gpu/vertex_buffers.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_mesh.h"
#include <d3d9.h>
#include <span>
#include <functional>
#include <gsl/pointers>

namespace render {

constexpr int kPassFlagTransparent = 0x40; // a material whose first pass has this flag is drawn with the transparent meshes
constexpr int kIndexStoreBytes = 0x18;
constexpr int kIndexStoreUsage = 0x65;
constexpr std::size_t kStripScratchCount = 3072;
constexpr std::size_t kStripBreakSlots = 64;
constexpr int kStripBreakLimit = 64;
constexpr uint32_t kIndexMask = 0x7FFF;    // a strip index; the top bit marks a repeated one (a strip break)
constexpr int kIndexRepeatBit = 0x8000;

namespace {

// A mesh's material and what the list looks at.
MaterialRef* material_of(const MeshRef* mesh) { return mesh->material(); }
bool get_transparent(const MaterialRef* material)
{
    return (material != nullptr) && ((core::at(material->pass_flags(), 0) & kPassFlagTransparent) != 0U);
}
uint16_t& load_index(MeshRef* mesh) { return mesh->load_index(); }
uint16_t& get_index_count(MeshRef* mesh) { return core::at(mesh->index_counts(), 0); }
IdxStore*& index_buffer_of(MeshRef* mesh) { return core::at(mesh->index_buffers(), 0); }

// The index buffers (not rewritten yet; thiscall): Lock gives the indices (a copy kept in memory).
uint16_t* lock_indices(void* buffer)
{
    uint16_t* indices = nullptr;
    static_cast<IdxStore*>(buffer)->get_lock(0, 0, &indices, 0);
    return indices;
}
void unlock_indices(void* buffer) { static_cast<IdxStore*>(buffer)->get_unlock(); }
void release_index_buffer(void* buffer) { static_cast<IdxStore*>(buffer)->teardown(); }
gsl::owner<IdxStore*> new_index_buffer(uint32_t bytes)
{
    gsl::owner<void*> memory = crt::alloc_block(kIndexStoreBytes); // (the renderer's operator new: a jump to the C runtime's malloc)
    if (!memory) {
        return nullptr;
    }
    return memory::construct_in<IdxStore>(memory, bytes, 0, kIndexStoreUsage);
}
// A mesh's vertex buffers (+0xCC) and their release.
void* get_vertex_buffer(MeshRef* mesh, int index) { return mesh->fetch_vertex_store(index); }
void set_vertex_buffer(MeshRef* mesh, int index, void* buffer)
{
    mesh->set_vertex_buffer(index, static_cast<VertexBuffer*>(buffer));
}
void release_vertex_buffer(void* buffer) { static_cast<VertexBuffer*>(buffer)->teardown(); }
// A material's destructor body (not rewritten yet; thiscall).
void destroy_material(void* material) { static_cast<MaterialRef*>(material)->~MaterialRef(); }

// the rebuilt strip (0x1800 bytes: nothing of the original's refers to the bytes up to 0x007CC244)

// The rebuilt strip (0x007CAA40).
std::array<uint16_t, kStripScratchCount>& tri_strip_scratch()
{
    static std::array<uint16_t, kStripScratchCount> index_scratch{};
    return index_scratch;
}

// The C runtime's qsort (the game's, so equal meshes stay in the same order).
void get_qsort(void* base, uint32_t count, uint32_t size, int (*compare)(const void*, const void*))
{
    crt::get_qsort(base, count, size, compare);
}

// Iterate a hash table's values: the first (or next), null at the end.
void* next_value(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item()) {
        return nullptr;
    }
    return table->iter_item()->value;
}
void* get_first_value(lists::KeyMap* table)
{
    table->set_iter_bucket(-1);
    table->set_iter_item(nullptr);
    return next_value(table);
}

} // namespace

// 0x004DDD50
int compare_meshes(const void* a, const void* b)
{
    const MeshRef* mesh_a = *static_cast<MeshRef* const*>(a);
    const MeshRef* mesh_b = *static_cast<MeshRef* const*>(b);
    const MaterialRef* material_a = material_of(mesh_a);
    const MaterialRef* material_b = material_of(mesh_b);
    const float key_a = material_a->sort_key();
    const float key_b = material_b->sort_key();
    if (!(key_a == key_b)) { // (also not a number)
        return key_a > key_b ? 1 : -1;
    }
    if (material_a->sort_flag() != material_b->sort_flag()) {
        return material_a->sort_flag() != 0 ? 1 : -1;
    }
    if (material_a == material_b) {
        return 0;
    }
    const bool flag_a = (core::at(material_a->pass_flags(), 0) & 0x10) != 0;
    const bool flag_b = (core::at(material_b->pass_flags(), 0) & 0x10) != 0;
    if (flag_a != flag_b) {
        return flag_a ? 1 : -1;
    }
    // (then by pixel shader, as the original: its address; then by the material's address)
    constexpr std::less<const void*> kBefore{};
    if (mesh_a->pixel_shader() != mesh_b->pixel_shader()) {
        return kBefore(mesh_b->pixel_shader(), mesh_a->pixel_shader()) ? 1 : -1;
    }
    return kBefore(material_b, material_a) ? 1 : -1;
}

// 0x004DE300
SceneRef::SceneRef() = default;

// 0x004DE490
void SceneRef::destroy_body()
{
    if (lists::KeyMap* table = materials_) {
        if (table->buckets()) {
            table->empty_table();
            memory::Hub::dealloc(table->buckets());
            table->set_buckets(nullptr);
        }
        memory::Hub::dealloc(table);
    }
    for (MeshRef* const mesh : mesh_run()) {
        if (!mesh) {
            continue;
        }
        if (void* buffer = get_vertex_buffer(mesh, 0)) {
            release_vertex_buffer(buffer);
            set_vertex_buffer(mesh, 0, nullptr);
        }
    }
    if (meshes_) {
        memory::Hub::dealloc(static_cast<void*>(meshes_));
    }
    free_hierarchy(hierarchy_);
    if (arena_buffer_) {
        arena_buffer_->Release();
    }
}

// 0x004DE340
void SceneRef::update_sphere()
{
    const double half_x = (static_cast<double>(bounds_.max.x) - bounds_.min.x) * 0.5;
    const double half_y = (static_cast<double>(bounds_.max.y) - bounds_.min.y) * 0.5; // (kept in double precision)
    auto size_z = static_cast<float>(static_cast<double>(bounds_.max.z) - bounds_.min.z);
    auto half_x_float = static_cast<float>(half_x);
    const double half_z = static_cast<double>(size_z) * 0.5;
    core::at(centre_, 0) = static_cast<float>(static_cast<double>(half_x_float) + bounds_.min.x);
    core::at(centre_, 1) = static_cast<float>(half_y + bounds_.min.y);
    core::at(centre_, 2) = static_cast<float>(half_z + bounds_.min.z);
    radius_ = static_cast<float>(
        std::sqrt(((half_z * half_z) + (half_y * half_y)) + (static_cast<double>(half_x_float) * half_x_float)));
}

// 0x004DE3E0
void SceneRef::allocate_mesh_list()
{
    if (mesh_count_ > 0) {
        meshes_ = static_cast<MeshRef**>(memory::alloc(mesh_count_ * 4));
    }
}

// 0x004DDDF0
void SceneRef::mesh_total(std::span<MeshRef* const> list)
{
    for (const MeshRef* const mesh : list) {
        ++mesh_count_;
        if (get_transparent(material_of(mesh))) {
            ++transparent_count_;
        }
    }
}

// 0x004DDE20
void SceneRef::append_meshes(std::span<MeshRef* const> list)
{
    for (MeshRef* const mesh : list) {
        core::at(mesh_run(), filled++) = mesh;
    }
}

void SceneRef::recount()
{
    transparent_count_ = 0;
    first_transparent_ = mesh_count_;
    first_flagged_ = mesh_count_;
    flagged_count_ = 0;
    for (int i = 0; i < mesh_count_; ++i) {
        const MaterialRef* material = material_of(core::at(mesh_run(), i));
        if (!get_transparent(material)) {
            continue;
        }
        ++transparent_count_;
        first_transparent_ = std::min(first_transparent_, i);
        if (material->sort_flag() != 0U) {
            ++flagged_count_;
            first_flagged_ = std::min(first_flagged_, i);
        }
    }
}

// 0x004DDE60
void SceneRef::detach_meshes(std::span<MeshRef* const> list)
{
    int removed = 0;
    for (const MeshRef* const gone : list) {
        for (MeshRef*& mesh : mesh_run()) {
            if (mesh == gone) {
                mesh = nullptr;
                ++removed;
                break;
            }
        }
    }
    // Close each gap. (A null left in the list before is taken as one; if none is found it never ends.)
    while (removed > 0) {
        const int count = mesh_count_;
        int index = 0;
        while (index < mesh_count_ && (core::at(mesh_run(), index) != nullptr)) {
            ++index;
        }
        if (index >= mesh_count_) {
            continue;
        }
        if (index < count - 1) {
            std::memmove(static_cast<void*>(mesh_run().subspan(static_cast<size_t>(index)).data()),
                         static_cast<const void*>(mesh_run().subspan(static_cast<size_t>(index) + 1).data()),
                         (count - index - 1) * sizeof(MeshRef*));
        }
        --removed;
        --mesh_count_;
        --filled;
    }
    recount();
}

// 0x004DDFA0
void SceneRef::order_meshes()
{
    get_qsort(static_cast<void*>(meshes_), mesh_count_, sizeof(MeshRef*), &compare_meshes);
    recount();
}

// 0x004DE020
MeshRef* SceneRef::find_mesh(uint32_t index) const
{
    for (MeshRef* const mesh : mesh_run()) {
        if (load_index(mesh) == index) {
            return mesh;
        }
    }
    return nullptr;
}

// 0x004DE420
void* SceneRef::find_material(uint32_t checksum) const
{
    if (!materials_) {
        return nullptr;
    }
    for (void* material = get_first_value(materials_); material; material = next_value(materials_)) {
        if (*static_cast<uint32_t*>(material) == checksum) {
            return material;
        }
    }
    return nullptr;
}

// 0x004DE060
void SceneRef::break_strips(uint32_t mask, const StripBreak* records, uint32_t count) const
{
    if (count == 0 || mask == 0) {
        return;
    }
    // Breaks found per mesh load index. (The original's table has 64 entries; a larger index overruns it.)
    std::array<int, kStripBreakSlots> breaks = {};
    for (const StripBreak& record : std::span<const StripBreak>(records, count)) {
        if ((record.mask & mask) == 0U) {
            continue;
        }
        const uint32_t mesh_index = record.mesh_and_index_ >> 16;
        MeshRef* mesh = find_mesh(mesh_index);
        if (!mesh) {
            continue;
        }
        const uint32_t index_a = record.mesh_and_index_ & 0xFFFF;
        const uint32_t index_b = record.indices & 0xFFFF;
        const uint32_t index_c = record.indices >> 16;
        const std::span<uint16_t> indices(lock_indices(index_buffer_of(mesh)), get_index_count(mesh));
        uint32_t position = record.strip_index_;
        uint32_t first = core::at(indices, position) & kIndexMask;
        uint32_t second = core::at(indices, position + 1) & kIndexMask;
        for (position += 2; position < get_index_count(mesh); ++position) {
            const uint32_t third = core::at(indices, position) & 0x7FFF;
            if (first == index_a && second == index_c && third == index_b) {
                if (mesh_index < kStripBreakLimit) {
                    ++core::at(breaks, mesh_index);
                }
                core::at(indices, position - 1) |= kIndexRepeatBit;
                break;
            }
            first = second;
            second = third;
        }
        unlock_indices(index_buffer_of(mesh));
    }
    for (MeshRef* const mesh : mesh_run()) {
        const uint32_t index = load_index(mesh);
        if (index >= kStripBreakLimit || core::at(breaks, index) <= 0) {
            continue;
        }
        const std::span<const uint16_t> indices(lock_indices(index_buffer_of(mesh)), get_index_count(mesh));
        uint32_t written = 0;
        for (uint32_t i = 0; i < get_index_count(mesh); ++i) {
            const uint16_t value = core::at(indices, i) & 0x7FFF;
            core::at(tri_strip_scratch(), written++) = value;
            if ((core::at(indices, i) & kIndexRepeatBit) != 0) { // repeated: a strip break
                core::at(tri_strip_scratch(), written++) = value;
                core::at(tri_strip_scratch(), written++) = value;
            }
        }
        release_index_buffer(index_buffer_of(mesh));
        get_index_count(mesh) = static_cast<uint16_t>(written);
        gsl::owner<IdxStore*> buffer = new_index_buffer(written * 2);
        index_buffer_of(mesh) = buffer;
        uint16_t* rebuilt = lock_indices(buffer);
        std::memcpy(rebuilt, tri_strip_scratch().data(), get_index_count(mesh) * 2);
        unlock_indices(index_buffer_of(mesh));
    }
}

// 0x004DE560
void discard_scene(SceneRef* scene)
{
    if (lists::KeyMap* table = scene->materials()) {
        for (void* material = get_first_value(table); material; material = next_value(scene->materials())) {
            destroy_material(material);
            memory::Hub::dealloc(material);
        }
    }
    scene->destroy_body();
    memory::Hub::dealloc(scene);
}

} // namespace render
