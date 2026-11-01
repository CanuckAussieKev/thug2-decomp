// The engine's mesh, a skin's collision file name, the splat sets' slots and the vertex stack: see engine_mesh.h and
// engine_effects.h.

#include "engine/gfx/engine_mesh.h"

#include "engine/core/str.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/manager.h"
#include "engine/file/packs.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_glue.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "formats/collision_format.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace render {

// 0x00490A60
SubMesh::SubMesh() { core::at(collision_name_, 0) = 0; }

// 0x00490A90
void SubMesh::detach_hit_mesh()
{
    if (core::at(collision_name_, 0)) {
        packs::offload(collision_name_.data());
        core::at(collision_name_, 0) = 0;
    }
}

// 0x00490B60
SubMesh::~SubMesh() { detach_hit_mesh(); }

// 0x00490AB0
HierarchyNode* SubMesh::fetch_bone_tree() { return hierarchy_; }

// 0x00490AC0
uint32_t SubMesh::get_hierarchy_count() { return hierarchy_count_; }

// 0x00490B80
bool SubMesh::add_collision(uint8_t* data, size_t size)
{
    // Laid out through collision_format.h, as the scene's. Not original - a damaged file (its sections, objects or
    // trees not within it) is refused: no collision objects.
    collision_format::CollisionHeader header;
    collision_format::CollisionLayout layout;
    hit_shape_ = data + collision_format::kHeaderSize;
    if (!collision_format::lay_out_collision(data, size, &header, &layout) ||
        !collision_format::check_collision_objects(data, header, layout)) {
        collision_count_ = 0;
        return true;
    }
    collision_count_ = header.object_count;
    for (int i = 0; i < header.object_count; ++i) {
        auto* object = raw::field_at<TriMeshSource>(hit_shape_, i * collision_format::kObjectSize);
        object->relocate(0, static_cast<uint32_t>(layout.vertices), static_cast<uint32_t>(layout.vertex_bytes),
                         static_cast<uint32_t>(layout.faces), static_cast<uint32_t>(layout.nodes),
                         static_cast<uint32_t>(layout.face_lists));
        object->set_up_all_faces();
        box_.append_spot(object->lower_limit_);
        box_.append_spot(object->upper_limit_);
    }
    return true;
}

// 0x00490AD0
void skin_collision_file_name(char* out, const char* name)
{
    text::copy_unbounded(out, name); // (no length limit, as the original)
    char* extension = std::strstr(out, ".");
    if (!extension)
        extension = out + std::strlen(out);
    std::memcpy(extension, ".col.", 6);
    text::append_unbounded(out, platform_suffix());
}

// 0x00490CD0
int SplatSlots::oldest_slot(uint32_t count)
{
    int oldest = 0;
    int most = core::at(steps, 0) - static_cast<int>(core::at(alive, 0));
    for (uint32_t i = 0; i < count; ++i) {
        if (!core::at(alive, i))
            return static_cast<int>(i);
        const int age = core::at(steps, i) - static_cast<int>(core::at(alive, i));
        if (age > most) {
            most = age;
            oldest = static_cast<int>(i);
        }
    }
    return oldest;
}

// 0x00490D20
VertexStack::VertexStack() noexcept { std::memset(data.data(), 3, sizeof(data)); }

// 0x00490D50
void VertexStack::stack_on(const uint8_t* in)
{
    std::memcpy(data.data() + used, in, vertex_size_);
    used += vertex_size_;
}

} // namespace render
