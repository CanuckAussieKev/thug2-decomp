// A scene's mesh list and materials: see mesh_list.h.

#include "engine/gfx/mesh_list.h"

#include <windows.h>

#include <unknwn.h> // IUnknown

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/lists/key_map.h"
#include "platform/crt.h"

namespace render {

namespace {

// A mesh's material and what the list looks at.
MaterialRef* material_of(const MeshRef* mesh) { return mesh->material; }
bool get_transparent(const MaterialRef* material) { return material && (material->pass_flags_[0] & 0x40); }
uint16_t& load_index(MeshRef* mesh) { return mesh->load_index_; }
uint16_t& get_index_count(MeshRef* mesh) { return mesh->index_counts_[0]; }
IdxStore*& index_buffer_of(MeshRef* mesh) { return mesh->index_buffers_[0]; }

// The index buffers (not rewritten yet; thiscall): Lock gives the indices (a copy kept in memory).
uint16_t* lock_indices(void* buffer)
{
    uint16_t* indices = nullptr;
    static_cast<IdxStore*>(buffer)->get_lock(0, 0, &indices, 0);
    return indices;
}
void unlock_indices(void* buffer) { static_cast<IdxStore*>(buffer)->get_unlock(); }
void release_index_buffer(void* buffer) { static_cast<IdxStore*>(buffer)->teardown(); }
IdxStore* new_index_buffer(uint32_t bytes)
{
    void* memory = crt::alloc_block(0x18); // (the renderer's operator new: a jump to the C runtime's malloc)
    if (!memory)
        return nullptr;
    return memory::construct_in<IdxStore>(memory, bytes, 0, 0x65);
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
uint16_t g_index_scratch[3072];

// The rebuilt strip (0x007CAA40).
uint16_t* tri_strip_scratch() { return g_index_scratch; }

// The C runtime's qsort (the game's, so equal meshes stay in the same order).
void get_qsort(void* base, uint32_t count, uint32_t size, int (*compare)(const void*, const void*))
{
    crt::get_qsort(base, count, size, compare);
}

// Iterate a hash table's values: the first (or next), null at the end.
void* next_value(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return table->iter_item_->value;
}
void* get_first_value(lists::KeyMap* table)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
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
    const float key_a = material_a->sort_key_;
    const float key_b = material_b->sort_key_;
    if (!(key_a == key_b)) // (also not a number)
        return key_a > key_b ? 1 : -1;
    if (material_a->sort_flag_ != material_b->sort_flag_)
        return material_a->sort_flag_ != 0 ? 1 : -1;
    if (material_a == material_b)
        return 0;
    const bool flag_a = (material_a->pass_flags_[0] & 0x10) != 0;
    const bool flag_b = (material_b->pass_flags_[0] & 0x10) != 0;
    if (flag_a != flag_b)
        return flag_a ? 1 : -1;
    // (then by pixel shader, as the original: its address)
    auto order_a = reinterpret_cast<uint32_t>(mesh_a->pixel_shader_);
    auto order_b = reinterpret_cast<uint32_t>(mesh_b->pixel_shader_);
    if (order_a != order_b)
        return order_b < order_a ? 1 : -1;
    return reinterpret_cast<uintptr_t>(material_b) < reinterpret_cast<uintptr_t>(material_a) ? 1 : -1;
}

// 0x004DE300
SceneRef::SceneRef() = default;

// 0x004DE490
void SceneRef::destroy_body()
{
    if (lists::KeyMap* table = materials) {
        if (table->buckets_) {
            table->empty_table();
            memory::Hub::sole()->dealloc(table->buckets_);
            table->buckets_ = nullptr;
        }
        memory::Hub::sole()->dealloc(table);
    }
    for (int i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = meshes[i];
        if (!mesh)
            continue;
        if (void* buffer = get_vertex_buffer(mesh, 0)) {
            release_vertex_buffer(buffer);
            set_vertex_buffer(meshes[i], 0, nullptr);
        }
    }
    if (meshes)
        memory::Hub::sole()->dealloc(meshes);
    if (extra) // (delete[]: the records' destructor is empty)
        memory::Hub::sole()->dealloc(extra - 4);
    if (com_object_)
        static_cast<IUnknown*>(com_object_)->Release(); // (a COM object of the scene's)
}

// 0x004DE340
void SceneRef::update_sphere()
{
    const double half_x = (static_cast<double>(bounds.max.x) - bounds.min.x) * 0.5;
    const double half_y = (static_cast<double>(bounds.max.y) - bounds.min.y) * 0.5; // (kept in double precision)
    auto size_z = static_cast<float>(static_cast<double>(bounds.max.z) - bounds.min.z);
    auto half_x_float = static_cast<float>(half_x);
    const double half_z = static_cast<double>(size_z) * 0.5;
    centre[0] = static_cast<float>(static_cast<double>(half_x_float) + bounds.min.x);
    centre[1] = static_cast<float>(half_y + bounds.min.y);
    centre[2] = static_cast<float>(half_z + bounds.min.z);
    radius = static_cast<float>(
        std::sqrt((half_z * half_z + half_y * half_y) + static_cast<double>(half_x_float) * half_x_float));
}

// 0x004DE3E0
void SceneRef::allocate_mesh_list()
{
    if (mesh_count_ > 0)
        meshes = static_cast<MeshRef**>(memory::alloc(mesh_count_ * 4));
}

// 0x004DDDF0
void SceneRef::mesh_total(int count, MeshRef* const* list)
{
    for (int i = 0; i < count; ++i) {
        ++mesh_count_;
        if (get_transparent(material_of(list[i])))
            ++transparent_count_;
    }
}

// 0x004DDE20
void SceneRef::append_meshes(int count, MeshRef* const* list)
{
    for (int i = 0; i < count; ++i)
        meshes[filled++] = list[i];
}

void SceneRef::recount()
{
    transparent_count_ = 0;
    first_transparent_ = mesh_count_;
    first_flagged_ = mesh_count_;
    flagged_count_ = 0;
    for (int i = 0; i < mesh_count_; ++i) {
        const MaterialRef* material = material_of(meshes[i]);
        if (!get_transparent(material))
            continue;
        ++transparent_count_;
        if (i < first_transparent_)
            first_transparent_ = i;
        if (material_of(meshes[i])->sort_flag_) {
            ++flagged_count_;
            if (i < first_flagged_)
                first_flagged_ = i;
        }
    }
}

// 0x004DDE60
void SceneRef::detach_meshes(int count, MeshRef* const* list)
{
    int removed = 0;
    for (int k = 0; k < count; ++k) {
        for (int j = 0; j < mesh_count_; ++j) {
            if (meshes[j] == list[k]) {
                meshes[j] = nullptr;
                ++removed;
                break;
            }
        }
    }
    // Close each gap. (A null left in the list before is taken as one; if none is found it never ends.)
    while (removed > 0) {
        const int n = mesh_count_;
        int j = 0;
        while (j < mesh_count_ && meshes[j])
            ++j;
        if (j >= mesh_count_)
            continue;
        if (j < n - 1)
            std::memmove(&meshes[j], &meshes[j + 1], (n - j - 1) * sizeof(meshes[0]));
        --removed;
        --mesh_count_;
        --filled;
    }
    recount();
}

// 0x004DDFA0
void SceneRef::order_meshes()
{
    get_qsort(meshes, mesh_count_, sizeof(meshes[0]), &compare_meshes);
    recount();
}

// 0x004DE020
MeshRef* SceneRef::find_mesh(uint32_t index) const
{
    for (int i = 0; i < mesh_count_; ++i) {
        if (load_index(meshes[i]) == index)
            return meshes[i];
    }
    return nullptr;
}

// 0x004DE420
void* SceneRef::find_material(uint32_t checksum) const
{
    if (!materials)
        return nullptr;
    for (void* material = get_first_value(materials); material; material = next_value(materials)) {
        if (*static_cast<uint32_t*>(material) == checksum)
            return material;
    }
    return nullptr;
}

// 0x004DE060
void SceneRef::break_strips(uint32_t mask, const uint32_t* records, uint32_t count)
{
    if (count == 0 || mask == 0)
        return;
    // Breaks found per mesh load index. (The original's table has 64 entries; a larger index overruns it.)
    int breaks[64] = {};
    for (uint32_t r = 0; r < count; ++r, records += 4) {
        if (!(records[0] & mask))
            continue;
        const uint32_t mesh_index = records[1] >> 16;
        MeshRef* mesh = find_mesh(mesh_index);
        if (!mesh)
            continue;
        const uint32_t a = records[1] & 0xFFFF;
        const uint32_t b = records[2] & 0xFFFF;
        const uint32_t c = records[2] >> 16;
        uint16_t* indices = lock_indices(index_buffer_of(mesh));
        uint32_t i = records[3];
        uint32_t first = indices[i] & 0x7FFF, second = indices[i + 1] & 0x7FFF;
        for (i += 2; i < get_index_count(mesh); ++i) {
            const uint32_t third = indices[i] & 0x7FFF;
            if (first == a && second == c && third == b) {
                if (mesh_index < 64)
                    ++breaks[mesh_index];
                indices[i - 1] |= 0x8000;
                break;
            }
            first = second;
            second = third;
        }
        unlock_indices(index_buffer_of(mesh));
    }
    for (int m = 0; m < mesh_count_; ++m) {
        MeshRef* mesh = meshes[m];
        const uint32_t index = load_index(mesh);
        if (index >= 64 || breaks[index] <= 0)
            continue;
        const uint16_t* indices = lock_indices(index_buffer_of(mesh));
        uint32_t n = 0;
        for (uint32_t i = 0; i < get_index_count(mesh); ++i) {
            const uint16_t value = indices[i] & 0x7FFF;
            tri_strip_scratch()[n++] = value;
            if (indices[i] & 0x8000) { // repeated: a strip break
                tri_strip_scratch()[n++] = value;
                tri_strip_scratch()[n++] = value;
            }
        }
        release_index_buffer(index_buffer_of(mesh));
        get_index_count(mesh) = static_cast<uint16_t>(n);
        IdxStore* buffer = new_index_buffer(n * 2);
        index_buffer_of(mesh) = buffer;
        uint16_t* rebuilt = lock_indices(buffer);
        std::memcpy(rebuilt, tri_strip_scratch(), get_index_count(mesh) * 2);
        unlock_indices(index_buffer_of(mesh));
    }
}

// 0x004DE560
void discard_scene(SceneRef* scene)
{
    if (lists::KeyMap* table = scene->materials) {
        for (void* material = get_first_value(table); material; material = next_value(scene->materials)) {
            destroy_material(material);
            memory::Hub::sole()->dealloc(material);
        }
    }
    scene->destroy_body();
    memory::Hub::sole()->dealloc(scene);
}

} // namespace render
