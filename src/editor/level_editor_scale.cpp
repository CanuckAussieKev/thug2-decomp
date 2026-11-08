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
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <array>

#include "engine/gfx/render_device.h"
#include "editor/level_editor.h"
#include "engine/gfx/mesh_records.h"
#include "engine/gfx/model.h"
#include "engine/render/coll_movable.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/objects/collision_component.h"
#include "engine/objects/entity.h"
#include "platform/run_log.h"
#include "engine/render/coll_line.h"
#include "engine/objects/model_component.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/engine_geom.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/down_cast.h"
#include "engine/gfx/vertex_view.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec4.h"
#include "editor/level_editor_coll_tree.h"

namespace frontend::level_editor::scale_tool {

namespace {

constexpr double kNoUpperBound = 1e30; // starts the allowed-scale range's upper end out of reach
constexpr std::size_t kScaleTextSize = 48; // the "1.00x" style description
constexpr uint32_t kFvfShaderHandleLimit = 0x10000; // at or above this an FVF is a shader handle
constexpr uint32_t kMinVertexStride = 24; // position and normal
constexpr int kPositionBytes = 12; // a position: three floats
constexpr std::size_t kScaleFieldSize = 64; // " scale=..." with room
constexpr std::size_t kScalePrefixLength = 6; // "scale="
constexpr uint32_t kCompressedVertexBytes = 6U; // a compressed vertex's bytes
constexpr float kSixteenths = 16.0F;
constexpr int kMaxTreeDepth = 256; // a guard against a broken hit tree              // a packed vertex coordinate: sixteenths of a unit
constexpr uint32_t kPlainVertexBytes = 12U;
constexpr uint32_t kMeshCountLimit = 4096; // meshes accepted per original

} // namespace

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
    if (factor > 1.0 && high < factor) {
        factor = std::max(1.0, high); // (already past the limit: no further)
    }
    if (factor < 1.0 && low > factor) {
        factor = std::min(1.0, low);
    }
    return factor;
}

} // namespace

V3 allowed_factor(V3 factor, const std::vector<V3>& scales, const std::vector<V3>& limits, bool uniform)
{
    std::array<double, 3> out{};
    for (int axis = 0; axis < 3; ++axis) {
        const double axis_factor = axis_value(factor, axis);
        if (!(axis_factor > 0.0) || !std::isfinite(axis_factor)) {
            core::at(out, axis) = 1.0;
            continue;
        }
        double low = 0.0;
        double high = kNoUpperBound;
        for (size_t i = 0; i < scales.size(); ++i) {
            const double scale =
                axis_value(core::at(scales, i), axis) > 0.0F ? axis_value(core::at(scales, i), axis) : 1.0;
            const double limit =
                i < limits.size() ? std::min<double>(axis_value(core::at(limits, i), axis), kMax) : kMax;
            low = std::max(low, kMin / scale);
            high = std::min(high, limit / scale);
        }
        core::at(out, axis) = allowed_on_axis(axis_factor, low, high);
    }
    if (uniform) {
        // (the factor asked for is the same on every axis: the one nearest 1 is what all of them allow)
        const double asked_factor = axis_value(factor, 0);
        double common = core::at(out, 0);
        for (int axis = 1; axis < 3; ++axis) {
            common =
                asked_factor >= 1.0 ? std::min(common, core::at(out, axis)) : std::max(common, core::at(out, axis));
        }
        core::at(out, 0) = core::at(out, 1) = core::at(out, 2) = common;
    }
    return {.x = static_cast<float>(core::at(out, 0)), .y = static_cast<float>(core::at(out, 1)), .z = static_cast<float>(core::at(out, 2))};
}

float compressed_limit(float extent)
{
    if (!(extent > 0.0F)) {
        return kMax;
    }
    return std::min(kMax, kCompressedExtent / extent);
}

float confine(float scale)
{
    if (!(scale == scale)) {
        return 1.0F;
    }
    return std::max(kMin, std::min(kMax, scale));
}

int main_axis(V3 delta)
{
    const float x = std::fabs(delta.x);
    const float y = std::fabs(delta.y);
    const float z = std::fabs(delta.z);
    if (y >= x && y >= z) {
        return 1;
    }
    return x >= z ? 0 : 2;
}

V3 along_axis(int axis, float factor)
{
    return {.x = axis == 0 ? factor : 1.0F, .y = axis == 1 ? factor : 1.0F, .z = axis == 2 ? factor : 1.0F};
}

int nearest_axis(std::span<const V3, 3> axes, int world_axis)
{
    int best = world_axis;
    float best_dot = -1.0F;
    for (int i = 0; i < 3; ++i) {
        const float length = std::sqrt(get_dot(core::at(axes, i), core::at(axes, i)));
        const float distance = length > 0.0F ? std::fabs(axis_value(core::at(axes, i), world_axis)) / length : 0.0F;
        if (distance > best_dot) {
            best_dot = distance;
            best = i;
        }
    }
    return best;
}

std::string describe(V3 scale)
{
    std::array<char, kScaleTextSize> text{};
    if (is_uniform_scale(scale)) {
        text::format_to(text, "%.2fx", scale.x);
    } else {
        text::format_to(text, "%.2f x %.2f x %.2f", scale.x, scale.y, scale.z);
    }
    return text.data();
}

RebuildFrame frame_for(int copied_quarters, V3 copied_position, int quarters0, int quarters, V3 position, V3 scale,
                       float twist)
{
    // The copy was taken after `copied` quarter turns of the piece (scale 1): copy - from = Q_c (x - x0); now
    // x' - to = Q S (x - x0) = Q_t (Q_c S Q_c^-1) (copy - from), Q_t the turns since, Q_c S Q_c^-1 S seen through Q_c.
    const int copied = (copied_quarters - quarters0) & 3;
    return {.from = copied_position, .to = position, .turns = (quarters - copied_quarters) & 3, .copy_scale_ = turned_scale(scale, copied),
            .normal_turns_ = (quarters - quarters0) & 3, .twist = twist};
}

V3 get_rebuilt(V3 point, V3 from, V3 to, int quarters, V3 scale, float twist)
{
    const V3 turned = get_quarter_turn(per_axis(point - from, scale), quarters);
    return to + (twist == 0.0F ? turned : turn_about_up(turned, twist));
}

void rewrite_positions(uint8_t* data, uint32_t stride, uint32_t count, const float* original, const RebuildFrame& frame)
{
    const gpu::VertexWriter vertices(data, stride, count);
    const std::span<const float> positions(original, 3 * static_cast<std::size_t>(count));
    for (uint32_t vertex = 0; vertex < count; ++vertex) {
        const V3 point = get_rebuilt({.x = core::at(positions, 3 * vertex),
                                      .y = core::at(positions, (3 * vertex) + 1),
                                      .z = core::at(positions, (3 * vertex) + 2)},
                                     frame.from, frame.to, frame.turns, frame.copy_scale_, frame.twist);
        vertices.set_float3(vertex, 0, point.x, point.y, point.z);
    }
}

V3 scaled_normal(V3 normal, V3 scale, Heading heading)
{
    if (!is_unit_scale(scale)) {
        const V3 scaled = {.x = normal.x / scale.x, .y = normal.y / scale.y, .z = normal.z / scale.z};
        const float length = std::sqrt(get_dot(scaled, scaled));
        normal = length > 0.0F ? scaled * (1.0F / length) : scaled;
    }
    const V3 turned = get_quarter_turn(normal, heading.quarters);
    return heading.twist == 0.0F ? turned : turn_about_up(turned, heading.twist);
}

void rewrite_normals(uint8_t* data, NormalRun normal_run, const float* original, V3 scale, Heading heading)
{
    const auto stride = normal_run.stride;
    const auto count = normal_run.count;
    const auto offset = normal_run.offset;
    const gpu::VertexWriter vertices(data, stride, count);
    const std::span<const float> normals(original, 3 * static_cast<std::size_t>(count));
    for (uint32_t vertex = 0; vertex < count; ++vertex) {
        const V3 normal = scaled_normal({.x = core::at(normals, 3 * vertex),
                                         .y = core::at(normals, (3 * vertex) + 1),
                                         .z = core::at(normals, (3 * vertex) + 2)},
                                        scale, heading);
        vertices.set_float3(vertex, offset, normal.x, normal.y, normal.z);
    }
}

int surface_nudge(uint32_t fvf, uint32_t stride)
{
    constexpr uint32_t kPositionMask = 0x400E;
    constexpr uint32_t kXyz = 0x002;
    constexpr uint32_t kNormal = 0x010;
    // (an FVF's texture coordinate size bits are 0 for the scene's 2D coordinates: a larger value is a shader handle)
    if (fvf >= kFvfShaderHandleLimit || (fvf & kPositionMask) != kXyz || ((fvf & kNormal) == 0U) ||
        stride < kMinVertexStride) {
        return -1;
    }
    return kPositionBytes;
}

std::string get_token(V3 scale)
{
    if (is_unit_scale(scale)) {
        return "";
    }
    std::array<char, kScaleFieldSize> text{};
    if (is_uniform_scale(scale)) {
        text::format_to(text, " scale=%.9g", scale.x);
    } else {
        text::format_to(text, " scale=%.9g,%.9g,%.9g", scale.x, scale.y, scale.z);
    }
    return text.data();
}

bool read_token(const std::string& field, V3* scale)
{
    if (!field.starts_with("scale=") || field.size() == kScalePrefixLength) {
        return false;
    }
    std::array<float, 3> values{};
    const char* at = std::next(field.c_str(), kScalePrefixLength);
    // (one number, or exactly three separated by commas)
    for (int count = 0;;) {
        char* end = nullptr;
        const float value = std::strtof(at, &end);
        if (end == at || !(value > 0.0F) || !std::isfinite(value)) {
            return false;
        }
        core::at(values, count++) = value;
        if (*end == '\0') {
            if (count == 1) {
                *scale = get_uniform(core::at(values, 0));
            } else if (count == 3) {
                *scale = {.x = core::at(values, 0), .y = core::at(values, 1), .z = core::at(values, 2)};
            }
            return count == 1 || count == 3;
        }
        if (*end != ',' || count == 3) {
            return false;
        }
        at = std::next(end);
    }
}

// --- the collision's copy ---------------------------------------------------------------------------------------

namespace {

void keep_split_nodes(render::HitTreeNode* node, std::vector<CollisionCopy::SplitNode>* out, int depth)
{
    // (iterative down the greater side, as the game's own walks; a guard against a broken tree)
    while (node != nullptr && !node->is_end_node() && depth < kMaxTreeDepth) {
        out->push_back({.node = node, .split = node->axis_tag(), .link = node->chain_link()});
        keep_split_nodes(node->less_child(), out, depth + 1);
        node = node->greater_child();
        ++depth;
    }
}

uint32_t get_vertex_bytes(const render::TriMeshSource* data)
{
    return data->num_vertices() * (data->compressed() ? kCompressedVertexBytes : kPlainVertexBytes);
}

} // namespace

CollisionCopy copy_collision(render::TriMeshSource* data)
{
    CollisionCopy copy;
    copy.data = data;
    if (!data) {
        return copy;
    }
    copy.num_vertices_ = data->num_vertices();
    copy.compressed = data->compressed();
    copy.vertex_array_ = data->vertices();
    if (data->vertices()) {
        const std::span<const uint8_t> bytes(static_cast<const uint8_t*>(data->vertices()), get_vertex_bytes(data));
        copy.vertices.assign(bytes.begin(), bytes.end());
    }
    copy.min = data->bounds().min;
    copy.max = data->bounds().max;
    copy.rejection = data->unknown3c();
    copy.tree = data->bsp();
    const coll_tree::Storage storage = coll_tree::storage_of(copy.tree);
    if (!storage.nodes.empty()) {
        copy.tree_nodes_.assign(storage.nodes.begin(), storage.nodes.end());
        copy.tree_faces_ = storage.faces.data();
        copy.tree_face_copy_.assign(storage.faces.begin(), storage.faces.end());
        copy.listed_ = coll_tree::listed_faces(copy.tree);
    } else {
        keep_split_nodes(copy.tree, &copy.split_nodes_, 0);
    }
    return copy;
}

bool same_collision(const CollisionCopy& copy)
{
    const render::TriMeshSource* data = copy.data;
    return (data != nullptr) && data->vertices() == copy.vertex_array_ && data->num_vertices() == copy.num_vertices_ &&
           data->compressed() == copy.compressed && copy.vertices.size() == get_vertex_bytes(data);
}

namespace {

// Every vertex of a geometry written from `positions` and its box made `low` .. `high` (the engine's choose_raw_verts,
// 0x00415C10, as written here: the editor's plug-in reaches the record's arrays, not that function) - packed vertices in
// sixteenths from the box's minimum, towards zero.
void write_vertices(render::TriMeshSource* data, const std::vector<maths::Vec4>& positions, const maths::Vec4& low,
                    const maths::Vec4& high)
{
    data->bounds().min = {.x = low.x, .y = low.y, .z = low.z, .w = 1.0F};
    data->bounds().max = {.x = high.x, .y = high.y, .z = high.z, .w = 1.0F};
    if (data->compressed()) {
        const std::span<uint16_t> packed = data->packed_vertices();
        for (size_t i = 0; i < positions.size(); ++i) {
            const maths::Vec4& point = core::at(positions, i);
            core::at(packed, (3 * i) + 0) = static_cast<uint16_t>(static_cast<int>((point.x - low.x) * kSixteenths));
            core::at(packed, (3 * i) + 1) = static_cast<uint16_t>(static_cast<int>((point.y - low.y) * kSixteenths));
            core::at(packed, (3 * i) + 2) = static_cast<uint16_t>(static_cast<int>((point.z - low.z) * kSixteenths));
        }
        return;
    }
    const std::span<float> floats = data->float_vertices();
    for (size_t i = 0; i < positions.size(); ++i) {
        const maths::Vec4& point = core::at(positions, i);
        core::at(floats, (3 * i) + 0) = point.x;
        core::at(floats, (3 * i) + 1) = point.y;
        core::at(floats, (3 * i) + 2) = point.z;
    }
}

} // namespace

bool rebuild_collision(const CollisionCopy& copy, int quarters, V3 from, V3 to, V3 scale, float twist)
{
    render::TriMeshSource* data = copy.data;
    if (!data || !data->vertices()) {
        return true;
    }
    std::memcpy(data->vertices(), copy.vertices.data(), copy.vertices.size());
    data->bounds().min = copy.min;
    data->bounds().max = copy.max;
    data->set_unknown3c(copy.rejection);
    if (!copy.tree_nodes_.empty()) {
        std::ranges::copy(copy.tree_nodes_, copy.tree);
        std::ranges::copy(copy.tree_face_copy_, copy.tree_faces_);
    }
    for (const CollisionCopy::SplitNode& node : copy.split_nodes_) {
        node.node->set_axis_tag(node.split);
        node.node->set_chain_link(node.link);
    }
    const maths::Vec4 centre = {.x = from.x, .y = from.y, .z = from.z, .w = 0.0F};
    if (!is_unit_scale(scale)) {
        const maths::Vec4 factor = {.x = scale.x, .y = scale.y, .z = scale.z, .w = 1.0F};
        data->scale_tool(&centre, &factor);
    }
    if ((quarters & 3) != 0) {
        data->rotate_y(&centre, quarters & 3);
    }
    const V3 delta = to - from;
    if (delta.x != 0.0F || delta.y != 0.0F || delta.z != 0.0F) {
        const maths::Vec4 by = {.x = delta.x, .y = delta.y, .z = delta.z, .w = 0.0F};
        data->get_slide(&by);
    }
    if (twist == 0.0F) {
        return true;
    }
    // Any other angle: the vertices turned about `to` (the box made from them), then a tree for them in the old one's
    // place (coll_tree.h). Packed vertices must stay within 16 bits of sixteenths from the new box.
    std::vector<maths::Vec4> positions(data->num_vertices());
    for (size_t i = 0; i < positions.size(); ++i) {
        data->read_stored_vertex(static_cast<int>(i), &core::at(positions, i));
    }
    maths::Vec4 low = {.x = INFINITY, .y = INFINITY, .z = INFINITY, .w = 0.0F};
    maths::Vec4 high = {.x = -INFINITY, .y = -INFINITY, .z = -INFINITY, .w = 0.0F};
    for (maths::Vec4& position : positions) {
        const V3 turned = to + turn_about_up(V3{.x = position.x, .y = position.y, .z = position.z} - to, twist);
        position = {.x = turned.x, .y = turned.y, .z = turned.z, .w = 0.0F};
        low = {.x = std::min(low.x, turned.x), .y = std::min(low.y, turned.y), .z = std::min(low.z, turned.z), .w = 0.0F};
        high = {.x = std::max(high.x, turned.x), .y = std::max(high.y, turned.y), .z = std::max(high.z, turned.z), .w = 0.0F};
    }
    if (data->compressed() &&
        (high.x - low.x > kCompressedExtent || high.y - low.y > kCompressedExtent || high.z - low.z > kCompressedExtent)) {
        return false;
    }
    if (data->bsp() != nullptr && copy.tree_nodes_.empty()) {
        return false; // (a tree that can't be rewritten in place: the faces would be looked up by the old splits)
    }
    write_vertices(data, positions, low, high);
    if (data->bsp() != nullptr) {
        const coll_tree::Storage storage = {
            .nodes = std::span<render::HitTreeNode>(copy.tree, copy.tree_nodes_.size()),
            .faces = std::span<uint16_t>(copy.tree_faces_, copy.tree_face_copy_.size())};
        if (!coll_tree::rebuild(*data, copy.listed_, storage)) {
            return false;
        }
    }
    data->set_unknown3c(0); // (as the engine's own turns: line tests then reject nothing by distance)
    return true;
}

V3 collision_limit(const render::TriMeshSource* data, V3 scale)
{
    V3 limit = get_uniform(kMax);
    if (!data || !data->compressed()) {
        return limit;
    }
    if (scale.x > 0.0F) {
        limit.x = compressed_limit((data->bounds().max.x - data->bounds().min.x) / scale.x);
    }
    if (scale.y > 0.0F) {
        limit.y = compressed_limit((data->bounds().max.y - data->bounds().min.y) / scale.y);
    }
    if (scale.z > 0.0F) {
        limit.z = compressed_limit((data->bounds().max.z - data->bounds().min.z) / scale.z);
    }
    return limit;
}

// --- the game side: scenery -------------------------------------------------------------------------------------

namespace {

// A mesh's copy: each vertex buffer's positions (and normals, when its format has them), its bounding sphere and its
// position list (+0xAC / +0xB0 - moved with the mesh by MeshRef::move_to, not turned by turn_y: rewritten the same way,
// scaled).
struct MeshCopy {
    render::MeshData* mesh;
    uint16_t vertex_count_;
    uint8_t stride;
    int normal_offset_;
    std::vector<render::VertexBufferData*> buffers;
    std::vector<uint8_t*> data;
    std::vector<std::vector<float>> positions; // (per buffer: x, y, z a vertex)
    std::vector<std::vector<float>> normals;   // (per buffer, when normalOffset >= 0)
    V3 centre;
    float radius;
    uint32_t list_count_;
    float* list;
    std::vector<float> list_copy_;
};

gsl::owner<render::HitMesh*> coll_of(render::WorldCell* sector) { return sector->hit_body(); }
render::TriMeshSource* collision_data(render::WorldCell* sector)
{
    const render::HitMesh* coll = coll_of(sector);
    return coll ? coll->geometry() : nullptr;
}

// The mesh's vertex format (+0x38: a Direct3D FVF, or a built-in vertex shader's handle).
uint32_t vertex_format(const render::MeshData* mesh) { return mesh->fvf(); }

// The vertex buffer written back to the card (0x004D40E0 after the inlined lock of the whole buffer, as MeshRef::move_to).
void upload(render::VertexBufferData* buffer) { render::device().upload_vertex_buffer(buffer); }

// The largest of a per-axis scale (a sphere's radius grows by it).
float get_largest(V3 scale) { return std::max({scale.x, scale.y, scale.z}); }

} // namespace

struct Original {
    render::Geom* geom{};
    const void* mesh_list_{}; // (the geom's mesh array: render::Device::geom_mesh_list)
    uint32_t mesh_count_{};
    int quarters{};
    V3 position{};
    std::vector<MeshCopy> meshes;
    bool have_collision_{};
    CollisionCopy collision;
};

std::shared_ptr<Original> get_capture(render::WorldCell* sector, render::Geom* geom, int quarters, V3 position)
{
    auto original = std::make_shared<Original>();
    original->geom = geom;
    original->mesh_list_ = render::device().geom_mesh_list(geom);
    original->mesh_count_ = render::device().geom_mesh_count(geom);
    original->quarters = quarters;
    original->position = position;
    if (!original->mesh_list_ || original->mesh_count_ == 0 || original->mesh_count_ > kMeshCountLimit) {
        return nullptr;
    }
    for (uint32_t i = 0; i < original->mesh_count_; ++i) {
        render::MeshData* mesh = render::device().geom_mesh(geom, i);
        if (!mesh || mesh->stride() < kPositionBytes || mesh->vertex_buffer_count() > 3) {
            return nullptr;
        }
        MeshCopy copy = {.mesh = mesh,
                         .vertex_count_ = mesh->vertex_count(),
                         .stride = mesh->stride(),
                         .normal_offset_ = surface_nudge(vertex_format(mesh), mesh->stride()),
                         .buffers = {},
                         .data = {},
                         .positions = {},
                         .normals = {},
                         .centre = {.x = core::at(mesh->centre(), 0), .y = core::at(mesh->centre(), 1), .z = core::at(mesh->centre(), 2)},
                         .radius = mesh->radius(),
                         .list_count_ = mesh->position_count(),
                         .list = mesh->positions(),
                         .list_copy_ = {}};
        for (int buffer_index = 0; std::cmp_less(buffer_index, mesh->vertex_buffer_count()); ++buffer_index) {
            render::VertexBufferData* buffer = core::at(mesh->vertex_buffers(), buffer_index);
            if (!buffer || !buffer->data() ||
                buffer->size() < static_cast<uint32_t>(mesh->vertex_count()) * mesh->stride()) {
                return nullptr;
            }
            std::vector<float> positions(size_t{3} * mesh->vertex_count());
            std::vector<float> normals(copy.normal_offset_ >= 0 ? 3U * mesh->vertex_count() : 0U);
            const std::span<const uint8_t> vertex_run(buffer->data(), size_t{mesh->vertex_count()} * mesh->stride());
            for (uint32_t vertex_index = 0; vertex_index < mesh->vertex_count(); ++vertex_index) {
                const std::span<const uint8_t> vertex =
                    vertex_run.subspan(size_t{vertex_index} * mesh->stride(), mesh->stride());
                std::memcpy(&core::at(positions, 3U * vertex_index), vertex.data(), kPositionBytes);
                if (copy.normal_offset_ >= 0) {
                    std::memcpy(&core::at(normals, 3U * vertex_index),
                                vertex.subspan(static_cast<size_t>(copy.normal_offset_)).data(), kPositionBytes);
                }
            }
            copy.buffers.emplace_back(buffer);
            copy.data.push_back(buffer->data());
            copy.positions.push_back(std::move(positions));
            copy.normals.push_back(std::move(normals));
        }
        if (mesh->positions() && (mesh->position_count() != 0U)) {
            const std::span<const float> position_list(mesh->positions(), 3U * size_t{mesh->position_count()});
            copy.list_copy_.assign(position_list.begin(), position_list.end());
        }
        original->meshes.push_back(std::move(copy));
    }
    render::TriMeshSource* data = collision_data(sector);
    original->have_collision_ = (data != nullptr) && (data->vertices() != nullptr);
    if (original->have_collision_) {
        original->collision = copy_collision(data);
    }
    return original;
}

bool reassemble(const Original& original, render::WorldCell* sector, render::Geom* geom, const Pose& pose)
{
    // (still the sector copied: its meshes, buffers and collision where and as they were)
    if (geom != original.geom || render::device().geom_mesh_list(geom) != original.mesh_list_ ||
        render::device().geom_mesh_count(geom) != original.mesh_count_) {
        return false;
    }
    for (uint32_t i = 0; i < original.mesh_count_; ++i) {
        const MeshCopy& copy = core::at(original.meshes, i);
        const render::MeshData* mesh = render::device().geom_mesh(geom, i);
        if (mesh != copy.mesh || mesh->vertex_count() != copy.vertex_count_ || mesh->stride() != copy.stride ||
            mesh->vertex_buffer_count() != copy.buffers.size() || mesh->positions() != copy.list ||
            mesh->position_count() != copy.list_count_) {
            return false;
        }
        for (size_t buffer = 0; buffer < copy.buffers.size(); ++buffer) {
            if (core::at(mesh->vertex_buffers(), buffer) != core::at(copy.buffers, buffer) ||
                core::at(copy.buffers, buffer)->data() != core::at(copy.data, buffer)) {
                return false;
            }
        }
    }
    const render::TriMeshSource* data = collision_data(sector);
    if (original.have_collision_ && (data != original.collision.data || !same_collision(original.collision))) {
        return false;
    }

    const V3 scale = pose.scale;
    const RebuildFrame frame =
        frame_for(original.quarters, original.position, pose.quarters0, pose.quarters, pose.position, scale, pose.twist);
    for (const MeshCopy& copy : original.meshes) {
        render::MeshData* mesh = copy.mesh;
        for (size_t buffer = 0; buffer < copy.buffers.size(); ++buffer) {
            rewrite_positions(core::at(copy.buffers, buffer)->data(), copy.stride, copy.vertex_count_,
                              core::at(copy.positions, buffer).data(), frame);
            if (copy.normal_offset_ >= 0) {
                rewrite_normals(core::at(copy.buffers, buffer)->data(),
                                {.stride = copy.stride,
                                 .count = copy.vertex_count_,
                                 .offset = static_cast<uint32_t>(copy.normal_offset_)},
                                core::at(copy.normals, buffer).data(), scale,
                                {.quarters = frame.normal_turns_, .twist = frame.twist});
            }
            upload(core::at(copy.buffers, buffer));
        }
        const V3 centre =
            get_rebuilt(copy.centre, frame.from, frame.to, frame.turns, frame.copy_scale_, frame.twist);
        core::at(mesh->centre(), 0) = centre.x;
        core::at(mesh->centre(), 1) = centre.y;
        core::at(mesh->centre(), 2) = centre.z;
        mesh->set_radius(copy.radius * get_largest(scale));
        const std::span<float> list = copy.list_copy_.empty()
                                          ? std::span<float>()
                                          : std::span<float>(mesh->positions(), 3U * size_t{copy.list_count_});
        for (uint32_t k = 0; k < copy.list_count_ && !copy.list_copy_.empty(); ++k) {
            const V3 point = get_rebuilt({.x = core::at(copy.list_copy_, 3 * k),
                                          .y = core::at(copy.list_copy_, (3 * k) + 1),
                                          .z = core::at(copy.list_copy_, (3 * k) + 2)},
                                         frame.from, frame.to, 0, frame.copy_scale_);
            core::at(list, (3 * k) + 0) = point.x;
            core::at(list, (3 * k) + 1) = point.y;
            core::at(list, (3 * k) + 2) = point.z;
        }
    }
    if (original.have_collision_ &&
        !rebuild_collision(original.collision, frame.turns, frame.from, frame.to, frame.copy_scale_, frame.twist)) {
        get_log("level editor: sector %p's collision couldn't be turned to that angle (left at its quarter turn)",
                static_cast<void*>(sector));
    }
    return true;
}

bool twist_fits(render::WorldCell* sector)
{
    const render::TriMeshSource* data = sector ? collision_data(sector) : nullptr;
    if (!data || !data->vertices()) {
        return true;
    }
    if (data->bsp() != nullptr && coll_tree::storage_of(data->bsp()).nodes.empty()) {
        return false;
    }
    if (!data->compressed()) {
        return true;
    }
    const float x = data->bounds().max.x - data->bounds().min.x;
    const float z = data->bounds().max.z - data->bounds().min.z;
    return std::hypot(x, z) <= kCompressedExtent;
}

V3 get_limit(render::WorldCell* sector, V3 world_scale)
{
    return sector ? collision_limit(collision_data(sector), world_scale) : get_uniform(kMax);
}

// --- the game side: objects -------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kModelModuleName = 0x286A8D26;
const void* const kListHeadData = memory::ListNode::head_marker();

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
std::vector<ObjectEntry>& stored_objects()
{
    static core::NoDestroy<std::vector<ObjectEntry>> objects;
    return objects.get();
}

render::RenderModel* model_of(objects::GameEntity* object)
{
    objects::ObjectModule* component = object->fetch_module(kModelModuleName);
    return component ? core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance().get()
                     : nullptr;
}

gsl::owner<render::HitMesh*> collision_of(objects::GameEntity* object)
{
    auto* component =
        core::down_cast<objects::ContactShapeModule>(object->fetch_module(objects::ContactShapeModule::kType));
    return component ? component->hit_shape() : nullptr;
}

bool get_alive(objects::GameEntity* object)
{
    return (object != nullptr) && ((object->object_flags() & objects::kObjectFlagDead) == 0U) &&
           ((object->composite_flags() & objects::kCompositeFlagDestroying) == 0U);
}

// The entry of an object, if it's still the object (and the collision) it was made for.
ObjectEntry* entry_of(objects::GameEntity* object)
{
    for (size_t i = 0; i < stored_objects().size(); ++i) {
        ObjectEntry& entry = core::at(stored_objects(), i);
        if (entry.id != object->ident()) {
            continue;
        }
        bool same = entry.object == object && get_alive(object) && collision_of(object) == entry.collision;
        for (const ObjectEntry::Tri& tri : entry.tris) {
            same = same && tri.tri->geometry() == tri.copy.data && same_collision(tri.copy);
        }
        if (same) {
            return &entry;
        }
        stored_objects().erase(stored_objects().begin() + static_cast<std::ptrdiff_t>(i)); // (another object of that name now)
        return nullptr;
    }
    return nullptr;
}

// A triangle object given a geometry of its own (unless it owns one already) and kept as copied.
bool take_tri(render::HitMovingTriangles* tri, ObjectEntry* entry)
{
    render::TriMeshSource* geometry = tri->geometry();
    if (!geometry) {
        return true;
    }
    const bool owns = ((tri->flag_bits() & render::HitMesh::kOwnsGeometry) != 0) &&
                      ((tri->flag_bits() & render::HitMesh::kSharedGeometry) == 0);
    if (!owns) {
        render::TriMeshSource* own = geometry->replicate(0, false);
        if (!own) {
            return false;
        }
        tri->flag_bits() = static_cast<uint16_t>((tri->flag_bits() | render::HitMesh::kOwnsGeometry) &
                                                ~render::HitMesh::kSharedGeometry);
        tri->assign_shape_data(own);
        geometry = own;
    }
    entry->tris.push_back({.tri = tri, .copy = copy_collision(geometry), .rejection = tri->unknownd0()});
    return true;
}

bool take_collision(render::HitMesh* collision, ObjectEntry* entry)
{
    if (!collision) {
        return true;
    }
    const render::HitMeshKind kind = collision->kind();
    if (kind == render::HitMeshKind::kMovingTriangles) {
        return take_tri(core::down_cast<render::HitMovingTriangles>(collision), entry);
    }
    if (kind == render::HitMeshKind::kMovingBox) {
        auto* box = core::down_cast<render::HitMovingBox>(collision);
        entry->boxes.push_back({.box = box, .original = box->box()});
        return true;
    }
    if (kind == render::HitMeshKind::kGroup) {
        const memory::ListNode* head = core::down_cast<render::HitMeshGroup>(collision)->sub_nodes();
        for (const memory::ListNode* node = head ? head->next() : nullptr;
             node && node != head && node->data() != kListHeadData; node = node->next()) {
            if (!take_collision(static_cast<render::HitMesh*>(node->data()), entry)) {
                return false;
            }
        }
        return true;
    }
    get_log("level editor: an object's collision of an unknown kind (%d) isn't scaled", static_cast<int>(kind));
    return true;
}

void apply_collision(ObjectEntry& entry)
{
    for (ObjectEntry::Tri& tri : entry.tris) {
        rebuild_collision(tri.copy, 0, {.x = 0.0F, .y = 0.0F, .z = 0.0F}, {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                          entry.scale);
        tri.tri->assign_shape_data(tri.copy.data); // (its radius from the vertices; its box made again)
        tri.tri->set_unknownd0(is_unit_scale(entry.scale) ? tri.rejection : tri.copy.data->unknown3c());
    }
    for (const ObjectEntry::Cuboid& box : entry.boxes) {
        const maths::Vec4& lo = box.original.min;
        const maths::Vec4& hi = box.original.max;
        box.box->box().min = {
            .x = lo.x * entry.scale.x, .y = lo.y * entry.scale.y, .z = lo.z * entry.scale.z, .w = lo.w};
        box.box->box().max = {
            .x = hi.x * entry.scale.x, .y = hi.y * entry.scale.y, .z = hi.z * entry.scale.z, .w = hi.w};
    }
    if (entry.collision && entry.collision->kind() == render::HitMeshKind::kGroup) {
        core::down_cast<render::HitMeshGroup>(entry.collision)->set_dirty(true);
    }
}

} // namespace

V3 get_object_scale(objects::GameEntity* object)
{
    const ObjectEntry* entry = object ? entry_of(object) : nullptr;
    return entry ? entry->scale : get_uniform(1.0F);
}

bool object_scalable(objects::GameEntity* object)
{
    return (object != nullptr) && get_alive(object) && (model_of(object) != nullptr);
}

V3 object_limit(objects::GameEntity* object)
{
    V3 limit = get_uniform(kMax);
    if (!object) {
        return limit;
    }
    const ObjectEntry* entry = entry_of(object);
    auto narrow = [&limit](const render::TriMeshSource* data, V3 scale) {
        const V3 mesh_limit = collision_limit(data, scale);
        limit = {.x = std::min(limit.x, mesh_limit.x),
                 .y = std::min(limit.y, mesh_limit.y),
                 .z = std::min(limit.z, mesh_limit.z)};
    };
    if (entry) {
        for (const ObjectEntry::Tri& tri : entry->tris) {
            narrow(tri.copy.data, entry->scale);
        }
        return limit;
    }
    std::vector<render::HitMesh*> pending = {collision_of(object)};
    while (!pending.empty()) {
        render::HitMesh* hit_mesh = pending.back();
        pending.pop_back();
        if (!hit_mesh) {
            continue;
        }
        if (hit_mesh->kind() == render::HitMeshKind::kMovingTriangles && hit_mesh->geometry()) {
            narrow(hit_mesh->geometry(), get_uniform(1.0F));
        }
        if (hit_mesh->kind() == render::HitMeshKind::kGroup) {
            const memory::ListNode* head = core::down_cast<render::HitMeshGroup>(hit_mesh)->sub_nodes();
            for (const memory::ListNode* node = head ? head->next() : nullptr;
                 node && node != head && node->data() != kListHeadData; node = node->next()) {
                pending.push_back(static_cast<render::HitMesh*>(node->data()));
            }
        }
    }
    return limit;
}

bool set_object_scale(objects::GameEntity* object, V3 scale)
{
    if (!object_scalable(object)) {
        return false;
    }
    ObjectEntry* entry = entry_of(object);
    if (!entry && is_unit_scale(scale)) {
        return true; // (never scaled: nothing to put back)
    }
    if (!entry) {
        ObjectEntry made = {.id = object->ident(),
                            .object = object,
                            .collision = collision_of(object),
                            .scale = get_uniform(1.0F),
                            .tris = {},
                            .boxes = {}};
        if (!take_collision(made.collision, &made)) {
            get_log("level editor: %08X's collision couldn't be copied - not scaled", object->ident());
            return false;
        }
        stored_objects().push_back(std::move(made));
        entry = &stored_objects().back();
    }
    const V3 limit = object_limit(object);
    scale = {.x = std::max(kMin, std::min(limit.x, confine(scale.x))), .y = std::max(kMin, std::min(limit.y, confine(scale.y))),
             .z = std::max(kMin, std::min(limit.z, confine(scale.z)))};
    entry->scale = scale;
    // The model drawn scaled (RenderModel::change_scale: the scale at +0x1C, flag 8 of +0x44 unless 1, 1, 1).
    const maths::Vec4 factor = {.x = scale.x, .y = scale.y, .z = scale.z, .w = 1.0F};
    model_of(object)->change_scale(&factor);
    apply_collision(*entry);
    object->mark_jumped_ahead(true); // (its components placed again at once)
    return true;
}

void forget_objects() { stored_objects().clear(); }

} // namespace frontend::level_editor::scale_tool
