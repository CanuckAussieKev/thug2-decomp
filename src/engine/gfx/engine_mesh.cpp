// The engine's mesh, a skin's collision file name, the splat sets' slots and the vertex stack: see engine_mesh.h and
// engine_effects.h.

#include "engine/gfx/engine_mesh.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/file/packs.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_glue.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "engine/core/code_address.h"

namespace render {

namespace {

extern void** const kMeshVtable;
constexpr uint32_t kObjectSize = 0x40;

uint32_t justify(uint32_t value, uint32_t to) { return (value + to - 1) & ~(to - 1); }

} // namespace

// 0x00490A60
SubMesh* SubMesh::make_new()
{
    vtable_ = reinterpret_cast<void**>(kMeshVtable);
    new (&box_) maths::BBox();
    unknownb0_ = 0;
    hierarchy_ = nullptr;
    hierarchy_count_ = 0;
    collision_name_[0] = 0;
    return this;
}

// 0x00490A90
void SubMesh::detach_hit_mesh()
{
    if (collision_name_[0]) {
        packs::offload(collision_name_);
        collision_name_[0] = 0;
    }
}

// 0x00490B60
void SubMesh::destroy_body()
{
    vtable_ = reinterpret_cast<void**>(kMeshVtable);
    detach_hit_mesh();
}

// 0x00490C90
SubMesh* SubMesh::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00490AB0
void* SubMesh::fetch_bone_tree()
{
    return hierarchy_;
}

// 0x00490AC0
uint32_t SubMesh::get_hierarchy_count()
{
    return hierarchy_count_;
}

// 0x00490B80
bool SubMesh::add_collision(const uint8_t* data)
{
    auto word = [data](int offset) { return *reinterpret_cast<const uint32_t*>(data + offset); };
    collision_count_ = static_cast<int>(word(4));
    hit_shape_ = const_cast<uint8_t*>(data) + 0x20;
    uint32_t objects = reinterpret_cast<uint32_t>(hit_shape_);
    uint32_t vertices = justify((static_cast<uint32_t>(collision_count_) << 6) + objects, 16);
    uint32_t vertex_bytes = vertices + 2 * (3 * (word(0x18) + 2 * word(0x14)));
    uint32_t faces = justify(word(8) + vertex_bytes, 4);
    uint32_t after_faces = justify(word(0xC) * 11 + word(0x10) * 8, 4) + faces;
    uint32_t nodes = after_faces + 4;
    uint32_t face_lists = *reinterpret_cast<const uint32_t*>(after_faces) + nodes;
    for (int i = 0; i < static_cast<int>(word(4)); ++i) {
        auto* object = reinterpret_cast<TriMeshSource*>(hit_shape_ + i * kObjectSize);
        object->relocate(0, vertices, vertex_bytes, faces, nodes, face_lists);
        object->set_up_all_faces();
        box_.append_spot(*reinterpret_cast<const maths::Vec4*>(hit_shape_ + i * kObjectSize + 0x10));
        box_.append_spot(*reinterpret_cast<const maths::Vec4*>(hit_shape_ + i * kObjectSize + 0x20));
    }
    return true;
}

// 0x00490AD0
void __cdecl skin_collision_file_name(char* out, const char* name)
{
    char* end = out;
    while ((*end++ = *name++) != '\0') { // (no length limit, as the original)
    }
    char* extension = std::strstr(out, ".");
    if (!extension)
        extension = out + std::strlen(out);
    std::memcpy(extension, ".col.", 6);
    const char* platform = platform_suffix();
    end = out + std::strlen(out);
    while ((*end++ = *platform++) != '\0') {
    }
}

// 0x00490CD0
int SplatSlots::oldest_slot(uint32_t count)
{
    int oldest = 0;
    int most = steps[0] - static_cast<int>(alive[0]);
    for (uint32_t i = 0; i < count; ++i) {
        if (!alive[i])
            return static_cast<int>(i);
        int age = steps[i] - static_cast<int>(alive[i]);
        if (age > most) {
            most = age;
            oldest = static_cast<int>(i);
        }
    }
    return oldest;
}

// 0x00490D20
void VertexStack::make_new()
{
    used = 0;
    vertex_size_ = 0;
    std::memset(data, 3, sizeof(data));
}

// 0x00490D50
void VertexStack::stack_on(const uint8_t* in)
{
    std::memcpy(data + used, in, vertex_size_);
    used += vertex_size_;
}

namespace {
static VtableSlot g_mesh_vtable_slots[] = {&render::SubMesh::teardown};
void** const kMeshVtable = reinterpret_cast<void**>(g_mesh_vtable_slots);
} // namespace

} // namespace Nx
