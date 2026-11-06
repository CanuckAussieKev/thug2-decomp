// The engine's mesh, a skin's collision file name, the splat sets' slots and the vertex stack: see engine_mesh.h and
// engine_effects.h.

#include "engine/gfx/engine_mesh.h"

#include "base/core/str.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <cstring>
#include <new>

#include "engine/file/packs.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/engine_glue.h"
#include "engine/render/coll_tri.h"
#include "formats/collision_format.h"
#include "base/core/checked_index.h"

namespace render {

// 0x004B2D30
HierarchyNode::HierarchyNode()
{
    matrix_.row = {{{.x = 1.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F},
                    {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F},
                    {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F},
                    {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F}}};
}

constexpr std::size_t kColExtensionBytes = 6; // ".col." and its terminator

// 0x00490A60
SubMesh::SubMesh() { core::at(collision_name_, 0) = 0; }

// 0x00490A90
void SubMesh::detach_hit_mesh()
{
    if (core::at(collision_name_, 0) != '\0') {
        packs::offload(collision_name_.data());
        core::at(collision_name_, 0) = 0;
    }
}

// 0x00490B60
SubMesh::~SubMesh() { detach_hit_mesh(); }

// 0x00490AB0
HierarchyNode* SubMesh::fetch_bone_tree() { return hierarchy_; }

// 0x00490AC0
uint32_t SubMesh::get_hierarchy_count() const { return hierarchy_count_; }

// 0x00490B80
bool SubMesh::add_collision(uint8_t* data, size_t size)
{
    // Laid out through collision_format.h, as the scene's. Not original - a damaged file (its sections, objects or
    // trees not within it) is refused: no collision objects.
    collision_format::CollisionHeader header;
    collision_format::CollisionLayout layout;
    const std::span<uint8_t> file(data, size);
    // (the first object's record, as the original sets it, whether the file is whole or not)
    hit_shape_ = collision_format::first_object_record<TriMeshSource>(file);
    if (!collision_format::lay_out_collision(file, &header, &layout) ||
        !collision_format::check_collision_objects(file, header, layout)) {
        collision_count_ = 0;
        return true;
    }
    collision_count_ = header.object_count;
    for (int i = 0; i < header.object_count; ++i) {
        collision_format::relocate_object(file, i, layout);
        auto* object = collision_format::object_record<TriMeshSource>(file, i);
        static_cast<void>(object->set_up_all_faces());
        box_.append_spot(object->bounds().min);
        box_.append_spot(object->bounds().max);
    }
    return true;
}

// 0x00490AD0
void skin_collision_file_name(char* out, const char* name)
{
    text::copy_unbounded(out, name); // (no length limit, as the original)
    char* extension = std::strstr(out, ".");
    if (!extension) {
        extension = std::next(out, static_cast<std::ptrdiff_t>(std::strlen(out)));
    }
    std::memcpy(extension, ".col.", kColExtensionBytes);
    text::append_unbounded(out, platform_suffix());
}

// 0x00490D20
VertexStack::VertexStack() noexcept { std::memset(data_.data(), 3, sizeof(data_)); }

// 0x00490D50
void VertexStack::stack_on(const uint8_t* in)
{
    std::memcpy(std::span(data_).subspan(used_, vertex_size_).data(), in, vertex_size_);
    used_ += vertex_size_;
}

} // namespace render
