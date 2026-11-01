// NOT ORIGINAL - the level editor's scaling of scenery and objects (ours, added at the user's request). See
// level_editor_scale.h.

#include "editor/level_editor_scale.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/model.h"
#include "engine/render/coll_movable.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/objects/collision_component.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "engine/render/coll_line.h"
#include "engine/gfx/viewport.h"
#include "engine/objects/model_component.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/geom.h"

namespace frontend::level_editor::scale_tool {

// --- the pieces (pure) ------------------------------------------------------------------------------------------

void scale_about(Placement* placement, V3 pivot, V3 factor)
{
    // x'' = F (x' - pivot) + pivot, x' = Q S x + offset: x'' = Q (F' S) x + F (offset - pivot) + pivot, F' = F as seen
    // through Q (F Q = Q F': odd turns swap its x and z).
    placement->scale = per_axis(turned_scale(factor, placement->quarters), placement->scale);
    placement->offset = per_axis(placement->offset - pivot, factor) + pivot;
}

namespace {

// One axis of allowed_factor.
double allowed_on_axis(double factor, double low, double high)
{
    if (factor > 1.0 && high < factor)
        factor = std::max(1.0, high); // (already past the limit: no further)
    if (factor < 1.0 && low > factor)
        factor = std::min(1.0, low);
    return factor;
}

float get_axis(V3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }

} // namespace

V3 allowed_factor(V3 factor, const std::vector<V3>& scales, const std::vector<V3>& limits, bool uniform)
{
    double out[3];
    for (int a = 0; a < 3; ++a) {
        const double f = get_axis(factor, a);
        if (!(f > 0.0) || !std::isfinite(f)) {
            out[a] = 1.0;
            continue;
        }
        double low = 0.0, high = 1e30;
        for (size_t i = 0; i < scales.size(); ++i) {
            const double scale = get_axis(scales[i], a) > 0.0f ? get_axis(scales[i], a) : 1.0;
            const double limit = i < limits.size() ? std::min<double>(get_axis(limits[i], a), kMax) : kMax;
            low = std::max(low, kMin / scale);
            high = std::min(high, limit / scale);
        }
        out[a] = allowed_on_axis(f, low, high);
    }
    if (uniform) {
        // (the factor asked for is the same on every axis: the one nearest 1 is what all of them allow)
        const double f = get_axis(factor, 0);
        double common = out[0];
        for (int a = 1; a < 3; ++a)
            common = f >= 1.0 ? std::min(common, out[a]) : std::max(common, out[a]);
        out[0] = out[1] = out[2] = common;
    }
    return {static_cast<float>(out[0]), static_cast<float>(out[1]), static_cast<float>(out[2])};
}

float compressed_limit(float extent)
{
    if (!(extent > 0.0f))
        return kMax;
    return std::min(kMax, kCompressedExtent / extent);
}

float confine(float scale)
{
    if (!(scale == scale))
        return 1.0f;
    return std::max(kMin, std::min(kMax, scale));
}

int main_axis(V3 d)
{
    const float x = std::fabs(d.x);
    const float y = std::fabs(d.y);
    const float z = std::fabs(d.z);
    if (y >= x && y >= z)
        return 1;
    return x >= z ? 0 : 2;
}

V3 along_axis(int axis, float f) { return {axis == 0 ? f : 1.0f, axis == 1 ? f : 1.0f, axis == 2 ? f : 1.0f}; }

int nearest_axis(const V3 axes[3], int world_axis)
{
    int best = world_axis;
    float best_dot = -1.0f;
    for (int i = 0; i < 3; ++i) {
        const float length = std::sqrt(get_dot(axes[i], axes[i]));
        const float d = length > 0.0f ? std::fabs(get_axis(axes[i], world_axis)) / length : 0.0f;
        if (d > best_dot) {
            best_dot = d;
            best = i;
        }
    }
    return best;
}

std::string describe(V3 scale)
{
    char text[48];
    if (is_uniform_scale(scale))
        std::snprintf(text, sizeof(text), "%.2fx", scale.x);
    else
        std::snprintf(text, sizeof(text), "%.2f x %.2f x %.2f", scale.x, scale.y, scale.z);
    return text;
}

RebuildFrame frame_for(int copied_quarters, V3 copied_position, int quarters0, int quarters, V3 position, V3 scale)
{
    // The copy was taken after `copied` quarter turns of the piece (scale 1): copy - from = Q_c (x - x0); now
    // x' - to = Q S (x - x0) = Q_t (Q_c S Q_c^-1) (copy - from), Q_t the turns since, Q_c S Q_c^-1 S seen through Q_c.
    const int copied = (copied_quarters - quarters0) & 3;
    return {copied_position, position, (quarters - copied_quarters) & 3, turned_scale(scale, copied),
            (quarters - quarters0) & 3};
}

V3 get_rebuilt(V3 p, V3 from, V3 to, int quarters, V3 scale)
{
    return to + get_quarter_turn(per_axis(p - from, scale), quarters);
}

void rewrite_positions(uint8_t* data, uint32_t stride, uint32_t count, const float* original, V3 from, V3 to,
                       int quarters, V3 scale)
{
    for (uint32_t v = 0; v < count; ++v, data += stride, original += 3) {
        const V3 p = get_rebuilt({original[0], original[1], original[2]}, from, to, quarters, scale);
        auto* out = reinterpret_cast<float*>(data);
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
    }
}

V3 scaled_normal(V3 n, V3 scale, int quarters)
{
    if (!is_unit_scale(scale)) {
        const V3 m = {n.x / scale.x, n.y / scale.y, n.z / scale.z};
        const float length = std::sqrt(get_dot(m, m));
        n = length > 0.0f ? m * (1.0f / length) : m;
    }
    return get_quarter_turn(n, quarters);
}

void rewrite_normals(uint8_t* data, uint32_t stride, uint32_t count, uint32_t offset, const float* original, V3 scale,
                     int quarters)
{
    for (uint32_t v = 0; v < count; ++v, data += stride, original += 3) {
        const V3 n = scaled_normal({original[0], original[1], original[2]}, scale, quarters);
        auto* out = reinterpret_cast<float*>(data + offset);
        out[0] = n.x;
        out[1] = n.y;
        out[2] = n.z;
    }
}

int surface_nudge(uint32_t fvf, uint32_t stride)
{
    constexpr uint32_t kPositionMask = 0x400E, kXyz = 0x002, kNormal = 0x010;
    // (an FVF's texture coordinate size bits are 0 for the scene's 2D coordinates: a larger value is a shader handle)
    if (fvf >= 0x10000 || (fvf & kPositionMask) != kXyz || !(fvf & kNormal) || stride < 24)
        return -1;
    return 12;
}

std::string get_token(V3 scale)
{
    if (is_unit_scale(scale))
        return "";
    char text[64];
    if (is_uniform_scale(scale))
        std::snprintf(text, sizeof(text), " scale=%.9g", scale.x);
    else
        std::snprintf(text, sizeof(text), " scale=%.9g,%.9g,%.9g", scale.x, scale.y, scale.z);
    return text;
}

bool read_token(const std::string& field, V3* scale)
{
    if (!field.starts_with("scale=") || field.size() == 6)
        return false;
    float values[3];
    const char* at = field.c_str() + 6;
    // (one number, or exactly three separated by commas)
    for (int count = 0;;) {
        char* end = nullptr;
        const float value = std::strtof(at, &end);
        if (end == at || !(value > 0.0f) || !std::isfinite(value))
            return false;
        values[count++] = value;
        if (*end == '\0') {
            if (count == 1)
                *scale = get_uniform(values[0]);
            else if (count == 3)
                *scale = {values[0], values[1], values[2]};
            return count == 1 || count == 3;
        }
        if (*end != ',' || count == 3)
            return false;
        at = end + 1;
    }
}

// --- the collision's copy ---------------------------------------------------------------------------------------

namespace {

void split_nodes(render::HitTreeNode* node, std::vector<CollisionCopy::Elem>* out, int depth)
{
    // (iterative down the greater side, as the game's own walks; a guard against a broken tree)
    while (node && !node->is_end_node() && depth < 256) {
        out->push_back({node, node->axis_tag_, node->chain_link_});
        split_nodes(node->less_child(), out, depth + 1);
        node = node->greater_child();
        ++depth;
    }
}

uint32_t get_vertex_bytes(const render::TriMeshSource* data)
{
    return data->num_vertices_ * (data->compressed_ ? 6u : 12u);
}

} // namespace

CollisionCopy copy_collision(render::TriMeshSource* data)
{
    CollisionCopy copy;
    copy.data = data;
    if (!data)
        return copy;
    copy.num_vertices_ = data->num_vertices_;
    copy.compressed = data->compressed_;
    copy.vertex_array_ = data->vertices_;
    if (data->vertices_) {
        const auto* bytes = static_cast<const uint8_t*>(data->vertices_);
        copy.vertices.assign(bytes, bytes + get_vertex_bytes(data));
    }
    copy.min = data->lower_limit_;
    copy.max = data->upper_limit_;
    copy.rejection = data->unknown3c_;
    split_nodes(data->bsp_, &copy.nodes, 0);
    return copy;
}

bool same_collision(const CollisionCopy& copy)
{
    const render::TriMeshSource* data = copy.data;
    return data && data->vertices_ == copy.vertex_array_ && data->num_vertices_ == copy.num_vertices_ &&
           data->compressed_ == copy.compressed && copy.vertices.size() == get_vertex_bytes(data);
}

void rebuild_collision(const CollisionCopy& copy, int quarters, V3 from, V3 to, V3 scale)
{
    render::TriMeshSource* data = copy.data;
    if (!data || !data->vertices_)
        return;
    std::memcpy(data->vertices_, copy.vertices.data(), copy.vertices.size());
    data->lower_limit_ = copy.min;
    data->upper_limit_ = copy.max;
    data->unknown3c_ = copy.rejection;
    for (const CollisionCopy::Elem& n : copy.nodes) {
        n.node->axis_tag_ = n.split;
        n.node->chain_link_ = n.link;
    }
    const maths::Vec4 centre = {from.x, from.y, from.z, 0.0f};
    if (!is_unit_scale(scale)) {
        const maths::Vec4 factor = {scale.x, scale.y, scale.z, 1.0f};
        data->scale_tool(&centre, &factor);
    }
    if (quarters & 3)
        data->rotate_y(&centre, quarters & 3);
    const V3 delta = to - from;
    if (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f) {
        const maths::Vec4 by = {delta.x, delta.y, delta.z, 0.0f};
        data->get_slide(&by);
    }
}

V3 collision_limit(const render::TriMeshSource* data, V3 scale)
{
    V3 limit = get_uniform(kMax);
    if (!data || !data->compressed_)
        return limit;
    if (scale.x > 0.0f)
        limit.x = compressed_limit((data->upper_limit_.x - data->lower_limit_.x) / scale.x);
    if (scale.y > 0.0f)
        limit.y = compressed_limit((data->upper_limit_.y - data->lower_limit_.y) / scale.y);
    if (scale.z > 0.0f)
        limit.z = compressed_limit((data->upper_limit_.z - data->lower_limit_.z) / scale.z);
    return limit;
}

// --- the game side: scenery -------------------------------------------------------------------------------------

namespace {

// A mesh's copy: each vertex buffer's positions (and normals, when its format has them), its bounding sphere and its
// position list (+0xAC / +0xB0 - moved with the mesh by MeshRef::move_to, not turned by turn_y: rewritten the same way,
// scaled).
struct MeshCopy {
    render::MeshRef* mesh;
    uint16_t vertex_count_;
    uint8_t stride;
    int normal_offset_;
    std::vector<render::VertexBuffer*> buffers;
    std::vector<uint8_t*> data;
    std::vector<std::vector<float>> positions; // (per buffer: x, y, z a vertex)
    std::vector<std::vector<float>> normals;   // (per buffer, when normalOffset >= 0)
    V3 centre;
    float radius;
    uint32_t list_count_;
    float* list;
    std::vector<float> list_copy_;
};

render::HitMesh* coll_of(render::WorldCell* sector) { return sector->hit_body_; }
render::TriMeshSource* collision_data(render::WorldCell* sector)
{
    const render::HitMesh* coll = coll_of(sector);
    return coll ? coll->geometry_ : nullptr;
}

// The mesh's vertex format (+0x38: a Direct3D FVF, or a built-in vertex shader's handle).
uint32_t vertex_format(const render::MeshRef* mesh) { return mesh->fvf; }

// The vertex buffer written back to the card (0x004D40E0 after the inlined lock of the whole buffer, as MeshRef::move_to).
void upload(render::VertexBuffer* buffer)
{
    buffer->lock_offset_ = 0;
    buffer->lock_size_ = buffer->size;
    buffer->lock_flags_ = 0;
    buffer->get_unlock();
}

// The largest of a per-axis scale (a sphere's radius grows by it).
float get_largest(V3 s) { return std::max({s.x, s.y, s.z}); }

} // namespace

struct Original {
    render::PcGeom* geom{};
    render::MeshRef** mesh_list_{};
    uint32_t mesh_count_{};
    int quarters{};
    V3 position{};
    std::vector<MeshCopy> meshes;
    bool have_collision_{};
    CollisionCopy collision;
};

std::shared_ptr<Original> get_capture(render::WorldCell* sector, render::PcGeom* geom, int quarters, V3 position)
{
    auto original = std::make_shared<Original>();
    original->geom = geom;
    original->mesh_list_ = geom->meshes;
    original->mesh_count_ = geom->mesh_count_;
    original->quarters = quarters;
    original->position = position;
    if (!original->mesh_list_ || original->mesh_count_ == 0 || original->mesh_count_ > 4096)
        return nullptr;
    for (uint32_t i = 0; i < original->mesh_count_; ++i) {
        render::MeshRef* mesh = original->mesh_list_[i];
        if (!mesh || mesh->stride < 12 || mesh->vertex_buffer_count_ > 3)
            return nullptr;
        MeshCopy copy = {mesh,
                         mesh->vertex_count_,
                         mesh->stride,
                         surface_nudge(vertex_format(mesh), mesh->stride),
                         {},
                         {},
                         {},
                         {},
                         {mesh->centre[0], mesh->centre[1], mesh->centre[2]},
                         mesh->radius,
                         mesh->position_count_,
                         mesh->positions,
                         {}};
        for (int b = 0; b < mesh->vertex_buffer_count_; ++b) {
            render::VertexBuffer* buffer = mesh->vertex_buffers_[b];
            if (!buffer || !buffer->data || buffer->size < static_cast<uint32_t>(mesh->vertex_count_) * mesh->stride)
                return nullptr;
            std::vector<float> positions(3u * mesh->vertex_count_);
            std::vector<float> normals(copy.normal_offset_ >= 0 ? 3u * mesh->vertex_count_ : 0u);
            const uint8_t* vertex = buffer->data;
            for (uint32_t v = 0; v < mesh->vertex_count_; ++v, vertex += mesh->stride) {
                std::memcpy(&positions[3u * v], vertex, 12);
                if (copy.normal_offset_ >= 0)
                    std::memcpy(&normals[3u * v], vertex + copy.normal_offset_, 12);
            }
            copy.buffers.emplace_back(buffer);
            copy.data.push_back(buffer->data);
            copy.positions.push_back(std::move(positions));
            copy.normals.push_back(std::move(normals));
        }
        if (mesh->positions && mesh->position_count_)
            copy.list_copy_.assign(mesh->positions, mesh->positions + 3u * mesh->position_count_);
        original->meshes.push_back(std::move(copy));
    }
    render::TriMeshSource* data = collision_data(sector);
    original->have_collision_ = data && data->vertices_;
    if (original->have_collision_)
        original->collision = copy_collision(data);
    return original;
}

bool reassemble(const Original& original, render::WorldCell* sector, render::PcGeom* geom, int quarters, V3 position,
                V3 scale, int quarters0)
{
    // (still the sector copied: its meshes, buffers and collision where and as they were)
    if (geom != original.geom || geom->meshes != original.mesh_list_ || geom->mesh_count_ != original.mesh_count_)
        return false;
    for (uint32_t i = 0; i < original.mesh_count_; ++i) {
        const MeshCopy& copy = original.meshes[i];
        const render::MeshRef* mesh = original.mesh_list_[i];
        if (mesh != copy.mesh || mesh->vertex_count_ != copy.vertex_count_ || mesh->stride != copy.stride ||
            mesh->vertex_buffer_count_ != copy.buffers.size() || mesh->positions != copy.list ||
            mesh->position_count_ != copy.list_count_)
            return false;
        for (size_t b = 0; b < copy.buffers.size(); ++b) {
            if (mesh->vertex_buffers_[b] != copy.buffers[b] || copy.buffers[b]->data != copy.data[b])
                return false;
        }
    }
    const render::TriMeshSource* data = collision_data(sector);
    if (original.have_collision_ && (data != original.collision.data || !same_collision(original.collision)))
        return false;

    const RebuildFrame f = frame_for(original.quarters, original.position, quarters0, quarters, position, scale);
    for (const MeshCopy& copy : original.meshes) {
        render::MeshRef* mesh = copy.mesh;
        for (size_t b = 0; b < copy.buffers.size(); ++b) {
            rewrite_positions(copy.buffers[b]->data, copy.stride, copy.vertex_count_, copy.positions[b].data(), f.from,
                              f.to, f.turns, f.copy_scale_);
            if (copy.normal_offset_ >= 0)
                rewrite_normals(copy.buffers[b]->data, copy.stride, copy.vertex_count_,
                                static_cast<uint32_t>(copy.normal_offset_), copy.normals[b].data(), scale,
                                f.normal_turns_);
            upload(copy.buffers[b]);
        }
        const V3 centre = get_rebuilt(copy.centre, f.from, f.to, f.turns, f.copy_scale_);
        mesh->centre[0] = centre.x;
        mesh->centre[1] = centre.y;
        mesh->centre[2] = centre.z;
        mesh->radius = copy.radius * get_largest(scale);
        for (uint32_t k = 0; k < copy.list_count_ && !copy.list_copy_.empty(); ++k) {
            const V3 p = get_rebuilt({copy.list_copy_[3 * k], copy.list_copy_[3 * k + 1], copy.list_copy_[3 * k + 2]},
                                     f.from, f.to, 0, f.copy_scale_);
            mesh->positions[3 * k + 0] = p.x;
            mesh->positions[3 * k + 1] = p.y;
            mesh->positions[3 * k + 2] = p.z;
        }
    }
    if (original.have_collision_)
        rebuild_collision(original.collision, f.turns, f.from, f.to, f.copy_scale_);
    return true;
}

V3 get_limit(render::WorldCell* sector, V3 world_scale)
{
    return sector ? collision_limit(collision_data(sector), world_scale) : get_uniform(kMax);
}

// --- the game side: objects -------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kModelModuleName = 0x286A8D26;
void* const kListHeadData = reinterpret_cast<void*>(-1);

// A scaled object: its collision object then, its triangle objects' own geometries (copied, as they were unscaled)
// and its boxes as they were, and the scale.
struct ObjectEntry {
    uint32_t id;
    objects::GameEntity* object;
    render::HitMesh* collision;
    V3 scale;
    struct Tri {
        render::HitMovingTriangles* tri;
        CollisionCopy copy;
        uint32_t rejection; // (the triangle object's own copy of the face-rejection scale, +0xD0)
    };
    std::vector<Tri> tris;
    struct Cuboid {
        render::HitMovingBox* box;
        maths::BBox original;
    };
    std::vector<Cuboid> boxes;
};
std::vector<ObjectEntry> g_objects;

uint8_t* model_of(objects::GameEntity* object)
{
    objects::ObjectModule* component = object->fetch_module(kModelModuleName);
    return component ? reinterpret_cast<uint8_t*>(static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_)
                     : nullptr;
}

render::HitMesh* collision_of(objects::GameEntity* object)
{
    auto* component =
        static_cast<objects::ContactShapeModule*>(object->fetch_module(objects::ContactShapeModule::kType));
    return component ? component->hit_shape_ : nullptr;
}

bool get_alive(objects::GameEntity* object)
{
    return object && !(object->object_flags_ & objects::kObjectFlagDead) &&
           !(object->composite_flags_ & objects::kCompositeFlagDestroying);
}

// The entry of an object, if it's still the object (and the collision) it was made for.
ObjectEntry* entry_of(objects::GameEntity* object)
{
    for (size_t i = 0; i < g_objects.size(); ++i) {
        ObjectEntry& entry = g_objects[i];
        if (entry.id != object->ident_)
            continue;
        bool same = entry.object == object && get_alive(object) && collision_of(object) == entry.collision;
        for (const ObjectEntry::Tri& t : entry.tris)
            same = same && t.tri->geometry_ == t.copy.data && same_collision(t.copy);
        if (same)
            return &entry;
        g_objects.erase(g_objects.begin() + static_cast<std::ptrdiff_t>(i)); // (another object of that name now)
        return nullptr;
    }
    return nullptr;
}

// A triangle object given a geometry of its own (unless it owns one already) and kept as copied.
bool take_tri(render::HitMovingTriangles* tri, ObjectEntry* entry)
{
    render::TriMeshSource* geometry = tri->geometry_;
    if (!geometry)
        return true;
    const bool owns =
        (tri->flag_bits_ & render::HitMesh::kOwnsGeometry) && !(tri->flag_bits_ & render::HitMesh::kSharedGeometry);
    if (!owns) {
        render::TriMeshSource* own = geometry->replicate(0, false);
        if (!own)
            return false;
        tri->flag_bits_ = static_cast<uint16_t>((tri->flag_bits_ | render::HitMesh::kOwnsGeometry) &
                                                ~render::HitMesh::kSharedGeometry);
        tri->assign_shape_data(own);
        geometry = own;
    }
    entry->tris.push_back({tri, copy_collision(geometry), tri->unknownd0_});
    return true;
}

bool take_collision(render::HitMesh* collision, ObjectEntry* entry)
{
    if (!collision)
        return true;
    const render::HitMeshKind kind = collision->kind();
    if (kind == render::HitMeshKind::kMovingTriangles)
        return take_tri(static_cast<render::HitMovingTriangles*>(collision), entry);
    if (kind == render::HitMeshKind::kMovingBox) {
        auto* box = static_cast<render::HitMovingBox*>(collision);
        entry->boxes.push_back({box, box->box_});
        return true;
    }
    if (kind == render::HitMeshKind::kGroup) {
        const memory::ListNode* head = static_cast<render::HitMeshGroup*>(collision)->sub_nodes_;
        for (const memory::ListNode* node = head ? head->next : nullptr;
             node && node != head && node->data != kListHeadData; node = node->next) {
            if (!take_collision(static_cast<render::HitMesh*>(node->data), entry))
                return false;
        }
        return true;
    }
    get_log("level editor: an object's collision of an unknown kind (%d) isn't scaled", static_cast<int>(kind));
    return true;
}

void apply_collision(ObjectEntry& entry)
{
    for (ObjectEntry::Tri& t : entry.tris) {
        rebuild_collision(t.copy, 0, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, entry.scale);
        t.tri->assign_shape_data(t.copy.data); // (its radius from the vertices; its box made again)
        t.tri->unknownd0_ = is_unit_scale(entry.scale) ? t.rejection : t.copy.data->unknown3c_;
    }
    for (const ObjectEntry::Cuboid& b : entry.boxes) {
        const maths::Vec4& lo = b.original.min;
        const maths::Vec4& hi = b.original.max;
        b.box->box_.min = {lo.x * entry.scale.x, lo.y * entry.scale.y, lo.z * entry.scale.z, lo.w};
        b.box->box_.max = {hi.x * entry.scale.x, hi.y * entry.scale.y, hi.z * entry.scale.z, hi.w};
    }
    if (entry.collision && entry.collision->kind() == render::HitMeshKind::kGroup)
        static_cast<render::HitMeshGroup*>(entry.collision)->dirty_ = true;
}

} // namespace

V3 get_object_scale(objects::GameEntity* object)
{
    const ObjectEntry* entry = object ? entry_of(object) : nullptr;
    return entry ? entry->scale : get_uniform(1.0f);
}

bool object_scalable(objects::GameEntity* object) { return object && get_alive(object) && model_of(object); }

V3 object_limit(objects::GameEntity* object)
{
    V3 limit = get_uniform(kMax);
    if (!object)
        return limit;
    const ObjectEntry* entry = entry_of(object);
    auto narrow = [&limit](const render::TriMeshSource* data, V3 scale) {
        const V3 l = collision_limit(data, scale);
        limit = {std::min(limit.x, l.x), std::min(limit.y, l.y), std::min(limit.z, l.z)};
    };
    if (entry) {
        for (const ObjectEntry::Tri& t : entry->tris)
            narrow(t.copy.data, entry->scale);
        return limit;
    }
    std::vector<render::HitMesh*> pending = {collision_of(object)};
    while (!pending.empty()) {
        render::HitMesh* c = pending.back();
        pending.pop_back();
        if (!c)
            continue;
        if (c->kind() == render::HitMeshKind::kMovingTriangles && c->geometry_)
            narrow(c->geometry_, get_uniform(1.0f));
        if (c->kind() == render::HitMeshKind::kGroup) {
            const memory::ListNode* head = static_cast<render::HitMeshGroup*>(c)->sub_nodes_;
            for (memory::ListNode* node = head ? head->next : nullptr;
                 node && node != head && node->data != kListHeadData; node = node->next)
                pending.push_back(static_cast<render::HitMesh*>(node->data));
        }
    }
    return limit;
}

bool set_object_scale(objects::GameEntity* object, V3 scale)
{
    if (!object_scalable(object))
        return false;
    ObjectEntry* entry = entry_of(object);
    if (!entry && is_unit_scale(scale))
        return true; // (never scaled: nothing to put back)
    if (!entry) {
        ObjectEntry made = {object->ident_, object, collision_of(object), get_uniform(1.0f), {}, {}};
        if (!take_collision(made.collision, &made)) {
            get_log("level editor: %08X's collision couldn't be copied - not scaled", object->ident_);
            return false;
        }
        g_objects.push_back(std::move(made));
        entry = &g_objects.back();
    }
    const V3 limit = object_limit(object);
    scale = {std::max(kMin, std::min(limit.x, confine(scale.x))), std::max(kMin, std::min(limit.y, confine(scale.y))),
             std::max(kMin, std::min(limit.z, confine(scale.z)))};
    entry->scale = scale;
    // The model drawn scaled (RenderModel::change_scale: the scale at +0x1C, flag 8 of +0x44 unless 1, 1, 1).
    const maths::Vec4 factor = {scale.x, scale.y, scale.z, 1.0f};
    reinterpret_cast<render::RenderModel*>(model_of(object))->change_scale(&factor);
    apply_collision(*entry);
    object->mark_jumped_ahead(true); // (its components placed again at once)
    return true;
}

void forget_objects() { g_objects.clear(); }

} // namespace frontend::level_editor::scale_tool
