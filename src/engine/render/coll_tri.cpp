// Collision geometry (TriMeshSource) and its BSP tree (HitTreeNode).

#include "engine/render/coll_tri.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_glue.h"
#include "engine/core/checked_index.h"
#include <algorithm>
#include <iterator>
#include <span>

namespace render {

namespace {

constexpr float kSixteenth = 0.0625f;
constexpr float kSixteen = 16.0f;

// The shared index table every face of a geometry is listed by when there's no tree to narrow them down.
std::array<uint16_t, 2048> g_all_faces{{0xFFFF}};
std::array<uint16_t, 2048>& all_faces() { return g_all_faces; }

std::span<uint16_t> g_face_buffer;
int g_face_count;

// get_faces_in_box's / is_point_behind_faces' state while walking the tree.
std::span<uint16_t>& s_face_buffer = g_face_buffer;
int& s_face_count = g_face_count;

// The compiler's float-to-integer conversion (_ftol: toward zero, the low 32 bits of the 64-bit result).
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

float get_component(const maths::Vec4* v, int axis) { return (*v)[axis]; }

} // namespace

// 0x0040B230
maths::Vec4 TriMeshSource::read_stored_vertex(int index) const
{
    if (compressed_) {
        const std::span<uint16_t, 3> v = packed_vertices().subspan(3 * static_cast<size_t>(index)).first<3>();
        return {.x = static_cast<float>(core::at(v, 0)) * 0.0625f + bounds_.min.x,
                .y = static_cast<float>(core::at(v, 1)) * 0.0625f + bounds_.min.y,
                .z = static_cast<float>(core::at(v, 2)) * 0.0625f + bounds_.min.z, .w = 0.0f};
    }
    const std::span<float, 3> v = float_vertices().subspan(3 * static_cast<size_t>(index)).first<3>();
    return {.x = core::at(v, 0), .y = core::at(v, 1), .z = core::at(v, 2), .w = 0.0f};
}

// 0x0040B2D0 (the uncompressed vertex copied as raw words)
void TriMeshSource::read_stored_vertex(int index, maths::Vec4* out) const
{
    if (compressed_) {
        const std::span<uint16_t, 3> v = packed_vertices().subspan(3 * static_cast<size_t>(index)).first<3>();
        out->x = static_cast<float>(core::at(v, 0)) * 0.0625f + bounds_.min.x;
        out->y = static_cast<float>(core::at(v, 1)) * 0.0625f + bounds_.min.y;
        out->z = static_cast<float>(core::at(v, 2)) * 0.0625f + bounds_.min.z;
        out->w = 0.0f;
        return;
    }
    const std::span<float, 3> v = float_vertices().subspan(3 * static_cast<size_t>(index)).first<3>();
    out->x = core::at(v, 0);
    out->y = core::at(v, 1);
    out->z = core::at(v, 2);
    out->w = 0.0f;
}

// 0x00414EE0 (x's offset isn't rounded to a float before it's scaled; y's and z's are)
void TriMeshSource::write_stored_vertex(int index, const maths::Vec4* position)
{
    if (compressed_) {
        const double x = static_cast<double>(position->x) - bounds_.min.x;
        auto y = static_cast<float>(static_cast<double>(position->y) - bounds_.min.y);
        auto z = static_cast<float>(static_cast<double>(position->z) - bounds_.min.z);
        const std::span<uint16_t, 3> v = packed_vertices().subspan(3 * static_cast<size_t>(index)).first<3>();
        core::at(v, 0) = static_cast<uint16_t>(truncate(x * kSixteen));
        core::at(v, 1) = static_cast<uint16_t>(truncate(static_cast<double>(y) * kSixteen));
        core::at(v, 2) = static_cast<uint16_t>(truncate(static_cast<double>(z) * kSixteen));
        return;
    }
    const std::span<float, 3> v = float_vertices().subspan(3 * static_cast<size_t>(index)).first<3>();
    core::at(v, 0) = position->x;
    core::at(v, 1) = position->y;
    core::at(v, 2) = position->z;
}

// 0x00414F90
void TriMeshSource::get_stored_vertex_array(std::span<maths::Vec4> out) const
{
    for (int i = 0; std::cmp_less(i, num_vertices_); ++i)
        core::at(out, i) = read_stored_vertex(i);
}

// 0x00415C10
void TriMeshSource::choose_raw_verts(std::span<const maths::Vec4> positions)
{
    maths::BBox* box = &bounds_;
    box->min = {.x = 10000000.0f, .y = 10000000.0f, .z = 10000000.0f, .w = 1.0f};
    box->max = {.x = -10000000.0f, .y = -10000000.0f, .z = -10000000.0f, .w = 1.0f};
    for (int i = 0; std::cmp_less(i, num_vertices_); ++i)
        box->append_spot(core::at(positions, i));
    for (int i = 0; std::cmp_less(i, num_vertices_); ++i)
        write_stored_vertex(i, &core::at(positions, i));
}

// 0x00414D80 (one of the edge's z kept unrounded for the normal's x, as the original's x87 code does)
maths::Vec4 TriMeshSource::get_triangle_normal(int face) const
{
    const maths::Vec4 v0 = read_stored_vertex(static_cast<int>(face_vertex(face, 0)));
    const maths::Vec4 v1 = read_stored_vertex(static_cast<int>(face_vertex(face, 1)));
    const maths::Vec4 v2 = read_stored_vertex(static_cast<int>(face_vertex(face, 2)));
    auto ax = static_cast<float>(static_cast<double>(v1.x) - v0.x);
    auto ay = static_cast<float>(static_cast<double>(v1.y) - v0.y);
    auto az = static_cast<float>(static_cast<double>(v1.z) - v0.z);
    auto bx = static_cast<float>(static_cast<double>(v2.x) - v0.x);
    auto by = static_cast<float>(static_cast<double>(v2.y) - v0.y);
    const double bz_exact = static_cast<double>(v2.z) - v0.z;
    auto bz = static_cast<float>(bz_exact);
    double nx = bz_exact * ay - static_cast<double>(by) * az;
    double ny = static_cast<double>(az) * bx - static_cast<double>(bz) * ax;
    double nz = static_cast<double>(by) * ax - static_cast<double>(ay) * bx;
    const double length = std::sqrt((nz * nz + ny * ny) + nx * nx);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        nx *= inverse;
        ny *= inverse;
        nz *= inverse;
    }
    return {.x = static_cast<float>(nx), .y = static_cast<float>(ny), .z = static_cast<float>(nz), .w = 0.0f};
}

// 0x00415B70
TriMeshSource* TriMeshSource::copy_from(const TriMeshSource* other)
{
    *this = *other;
    return this;
}

// 0x00416BF0 (a failed allocation of the copy itself crashes, as in the original; so does a missing tree)
TriMeshSource* TriMeshSource::replicate(int bsp_argument, bool need_vertices) const
{
    if (need_vertices && num_vertices_ == 0)
        return nullptr;
    void* memory = memory::alloc(sizeof(TriMeshSource));
    TriMeshSource* copy = memory ? static_cast<TriMeshSource*>(memory)->copy_from(this) : nullptr;
    const uint32_t vertex_size = num_vertices_ * (compressed_ ? 6u : 12u);
    copy->vertices_ = memory::alloc(vertex_size);
    const uint32_t face_size = tri_count_ * (small_faces_ ? 8u : 10u);
    copy->faces_ = static_cast<uint8_t*>(memory::alloc(face_size));
    if (!copy->vertices_ || !copy->faces_)
        return nullptr;
    std::memcpy(copy->vertices_, vertices_, vertex_size);
    std::memcpy(copy->faces_, faces_, face_size);
    if (vertex_bytes_) {
        copy->vertex_bytes_ = static_cast<uint8_t*>(memory::alloc(num_vertices_));
        std::memcpy(copy->vertex_bytes_, vertex_bytes_, num_vertices_);
    }
    copy->bsp_ = bsp_->replicate(bsp_argument);
    return copy;
}

// 0x00416970
void TriMeshSource::destruct()
{
    if (vertices_)
        memory::Hub::get_pull()->dealloc(vertices_);
    if (vertex_bytes_)
        memory::Hub::get_pull()->dealloc(vertex_bytes_);
    if (faces_)
        memory::Hub::get_pull()->dealloc(faces_);
    delete_bsp();
}

// 0x00416080 (the nodes' destructor - 0x005480A0, run by delete[] for each - is empty)
bool TriMeshSource::delete_bsp()
{
    if (!bsp_)
        return false;
    if (uint16_t* faces = bsp_->first_face_list())
        memory::Hub::get_pull()->dealloc(faces);
    if (bsp_)
        free_tree_nodes(bsp_);
    bsp_ = nullptr;
    return true;
}

HitTreeNode* new_tree_nodes(int count)
{
    // (the count's block is a node: one node more than asked for)
    auto* raw = static_cast<HitTreeNode*>(memory::alloc(sizeof(HitTreeNode) * (count + 1)));
    if (!raw)
        return nullptr;
    const std::span<HitTreeNode> block(raw, static_cast<size_t>(count) + 1);
    for (HitTreeNode& node : block)
        memory::construct_in<HitTreeNode>(&node);
    core::at(block, 0).axis_tag_ = count;
    return &core::at(block, 1);
}

// (the block starts one node before the first: the count's node)
void free_tree_nodes(HitTreeNode* first) { memory::Hub::get_pull()->dealloc(std::prev(first)); }

// 0x00416400
void TriMeshSource::get_slide(const maths::Vec4* delta)
{
    if (bsp_)
        bsp_->get_slide(delta);
    if (!compressed_) {
        const std::span<float> v = float_vertices();
        for (size_t k = 0; k < v.size(); k += 3) {
            float& x = core::at(v, k);
            float& y = core::at(v, k + 1);
            float& z = core::at(v, k + 2);
            x = static_cast<float>(static_cast<double>(x) + delta->x);
            y = static_cast<float>(static_cast<double>(y) + delta->y);
            z = static_cast<float>(static_cast<double>(delta->z) + z);
        }
    }
    auto moved = [delta](const maths::Vec4& p) {
        return maths::Vec4{.x = static_cast<float>(static_cast<double>(p.x) + delta->x),
                           .y = static_cast<float>(static_cast<double>(p.y) + delta->y),
                           .z = static_cast<float>(static_cast<double>(p.z) + delta->z),
                           .w = static_cast<float>(static_cast<double>(p.w) + delta->w)};
    };
    const maths::Vec4 min = moved(bounds_.min);
    const maths::Vec4 max = moved(bounds_.max);
    bounds_.min = min;
    bounds_.max = max;
}

// 0x004164F0 (a failed allocation of the temporary array crashes - the vertices are read into null - as in the
// original; the array's element constructor, 0x005902E0, does nothing)
void TriMeshSource::rotate_y(const maths::Vec4* centre, int quarter_turns)
{
    const maths::Vec4 back{.x = -centre->x, .y = -centre->y, .z = -centre->z, .w = -centre->w};
    get_slide(&back);
    if (bsp_)
        bsp_->rotate_y(centre, quarter_turns, false);
    maths::Vec4* positions = nullptr;
    std::span<maths::Vec4> position_run;
    if (compressed_) {
        memory::enter_heap_scope(memory::top_down_heap());
        positions = static_cast<maths::Vec4*>(memory::alloc(num_vertices_ * sizeof(maths::Vec4)));
        memory::leave_heap_scope();
        position_run = {positions, num_vertices_};
        get_stored_vertex_array(position_run);
    }
    for (int i = 0; std::cmp_less(i, num_vertices_); ++i) {
        if (positions) {
            core::at(position_run, i).turn_quarter_about_y(quarter_turns);
            continue;
        }
        maths::Vec4 v;
        read_stored_vertex(i, &v);
        v.turn_quarter_about_y(quarter_turns);
        const std::span<float, 3> stored = float_vertices().subspan(3 * static_cast<size_t>(i)).first<3>();
        core::at(stored, 0) = v.x;
        core::at(stored, 1) = v.y;
        core::at(stored, 2) = v.z;
    }
    maths::Vec4 min = bounds_.min;
    maths::Vec4 max = bounds_.max;
    min.turn_quarter_about_y(quarter_turns);
    max.turn_quarter_about_y(quarter_turns);
    maths::BBox* box = &bounds_;
    box->min = {.x = 10000000.0f, .y = 10000000.0f, .z = 10000000.0f, .w = 1.0f};
    box->max = {.x = -10000000.0f, .y = -10000000.0f, .z = -10000000.0f, .w = 1.0f};
    box->append_spot(min);
    box->append_spot(max);
    if (positions) {
        for (int i = 0; std::cmp_less(i, num_vertices_); ++i)
            write_stored_vertex(i, &core::at(position_run, i));
        memory::Hub::get_pull()->dealloc(positions);
    }
    get_slide(centre);
    unknown3c_ = 0;
}

// 0x00416730 (a failed allocation crashes, as in RotateY)
void TriMeshSource::scale_tool(const maths::Vec4* centre, const maths::Vec4* scale)
{
    const maths::Vec4 back{.x = -centre->x, .y = -centre->y, .z = -centre->z, .w = -centre->w};
    get_slide(&back);
    if (bsp_)
        bsp_->scale_tool(centre, scale, false);
    maths::Vec4* positions = nullptr;
    std::span<maths::Vec4> position_run;
    if (compressed_) {
        memory::enter_heap_scope(memory::top_down_heap());
        positions = static_cast<maths::Vec4*>(memory::alloc(num_vertices_ * sizeof(maths::Vec4)));
        memory::leave_heap_scope();
        position_run = {positions, num_vertices_};
        get_stored_vertex_array(position_run);
    }
    for (int i = 0; std::cmp_less(i, num_vertices_); ++i) {
        if (positions) {
            maths::Vec4& p = core::at(position_run, i);
            p.x *= scale->x;
            p.y *= scale->y;
            p.z *= scale->z;
        } else {
            const std::span<float, 3> v = float_vertices().subspan(3 * static_cast<size_t>(i)).first<3>();
            core::at(v, 0) *= scale->x;
            core::at(v, 1) *= scale->y;
            core::at(v, 2) *= scale->z;
        }
    }
    bounds_.min = {.x = bounds_.min.x * scale->x, .y = bounds_.min.y * scale->y, .z = bounds_.min.z * scale->z, .w = bounds_.min.w};
    bounds_.max = {.x = bounds_.max.x * scale->x, .y = bounds_.max.y * scale->y, .z = bounds_.max.z * scale->z, .w = bounds_.max.w};
    if (positions) {
        for (int i = 0; std::cmp_less(i, num_vertices_); ++i)
            write_stored_vertex(i, &core::at(position_run, i));
        memory::Hub::get_pull()->dealloc(positions);
    }
    get_slide(centre);
    unknown3c_ = 0;
}

// 0x00414D30
bool TriMeshSource::set_up_all_faces()
{
    auto& table = all_faces();
    if (core::at(table, 0) != 0) {
        for (int i = 0; i < 0x800; ++i)
            core::at(table, i) = static_cast<uint16_t>(i);
    }
    return bsp_ != nullptr;
}

// 0x00414FE0 (the original also reads each face's flags and tests a sign they can't have, and reads the first
// face's corners before each pass; neither has an effect)
void TriMeshSource::add_occlusion_polys()
{
    constexpr float kSameDirection = 0.05f;
    std::array<maths::Vec4, 4> points{};
    for (int i = 0; i < tri_count_ - 1; ++i) {
        for (int j = i + 1; std::cmp_less(j, tri_count_); ++j) {
            // the corners shared: shared[k] is face i's corner k if face j has it, other[m] face j's corner m if
            // face i has it
            std::array<int, 3> shared = {{-1, -1, -1}};
            std::array<int, 3> other = {{-1, -1, -1}};
            int matches = 0;
            for (int k = 0; k < 3; ++k) {
                auto corner = static_cast<uint16_t>(face_vertex(i, k));
                for (int m = 0; m < 3; ++m) {
                    if (corner == static_cast<uint16_t>(face_vertex(j, m))) {
                        core::at(shared, k) = corner;
                        core::at(other, m) = static_cast<int>(face_vertex(j, m));
                        ++matches;
                        break;
                    }
                }
            }
            if (matches != 2)
                continue;
            // both normals ((v1 - v0) x (v2 - v0)), rounded where the original's x87 code stores them
            const maths::Vec4 a = read_stored_vertex(static_cast<int>(face_vertex(i, 0)));
            const maths::Vec4 b = read_stored_vertex(static_cast<int>(face_vertex(i, 1)));
            const maths::Vec4 c = read_stored_vertex(static_cast<int>(face_vertex(i, 2)));
            const double cx = static_cast<double>(c.x) - a.x;
            const double cy = static_cast<double>(c.y) - a.y;
            const double cz = static_cast<double>(c.z) - a.z;
            auto bx = static_cast<float>(static_cast<double>(b.x) - a.x);
            auto by = static_cast<float>(static_cast<double>(b.y) - a.y);
            const double bz = static_cast<double>(b.z) - a.z;
            auto n1x = static_cast<float>(by * cz - bz * cy);
            auto n1y = static_cast<float>(bz * cx - cz * bx);
            auto n1z = static_cast<float>(cy * bx - by * cx);
            const maths::Vec4 a2 = read_stored_vertex(static_cast<int>(face_vertex(j, 0)));
            const maths::Vec4 b2 = read_stored_vertex(static_cast<int>(face_vertex(j, 1)));
            const maths::Vec4 c2 = read_stored_vertex(static_cast<int>(face_vertex(j, 2)));
            core::at(points, 0) = a2;
            core::at(points, 1) = b2;
            core::at(points, 2) = c2;
            auto difference = [](float p, float q) { return static_cast<float>(static_cast<double>(p) - q); };
            const float cx2 = difference(c2.x, a2.x);
            const float cy2 = difference(c2.y, a2.y);
            const float cz2 = difference(c2.z, a2.z);
            const float bx2 = difference(b2.x, a2.x);
            const float by2 = difference(b2.y, a2.y);
            const float bz2 = difference(b2.z, a2.z);
            double n2x = static_cast<double>(by2) * cz2 - static_cast<double>(bz2) * cy2;
            auto n2y = static_cast<float>(static_cast<double>(bz2) * cx2 - static_cast<double>(cz2) * bx2);
            auto n2z = static_cast<float>(static_cast<double>(cy2) * bx2 - static_cast<double>(by2) * cx2);
            double unit1x = n1x;
            const double length1 =
                std::sqrt((unit1x * n1x + static_cast<double>(n1z) * n1z) + static_cast<double>(n1y) * n1y);
            if (length1 > 0.0) {
                const double inverse = 1.0 / length1;
                unit1x = n1x * inverse;
                n1y = static_cast<float>(n1y * inverse);
                n1z = static_cast<float>(n1z * inverse);
            }
            const double length2 =
                std::sqrt((n2x * n2x + static_cast<double>(n2z) * n2z) + static_cast<double>(n2y) * n2y);
            if (length2 > 0.0) {
                const double inverse = 1.0 / length2;
                n2x *= inverse;
                n2y = static_cast<float>(n2y * inverse);
                n2z = static_cast<float>(n2z * inverse);
            }
            if (!(std::fabs(n2x - unit1x) <= kSameDirection) ||
                !(std::fabs(static_cast<double>(n2y) - n1y) <= kSameDirection) ||
                !(std::fabs(static_cast<double>(n2z) - n1z) <= kSameDirection))
                continue;
            // the quad: face i's corners with face j's other corner between the two shared ones
            int extra = -1;
            for (int m = 0; m < 3; ++m) {
                if (core::at(other, m) == -1) {
                    extra = static_cast<int>(face_vertex(j, m));
                    break;
                }
            }
            auto corner = [this, i](int k) { return read_stored_vertex(static_cast<int>(face_vertex(i, k))); };
            if (core::at(shared, 0) >= 0 && core::at(shared, 1) >= 0) {
                core::at(points, 0) = corner(0);
                core::at(points, 1) = read_stored_vertex(extra);
                core::at(points, 2) = corner(1);
                core::at(points, 3) = corner(2);
            } else if (core::at(shared, 0) < 0 && core::at(shared, 1) >= 0 && core::at(shared, 2) >= 0) {
                core::at(points, 0) = corner(0);
                core::at(points, 1) = corner(1);
                core::at(points, 2) = read_stored_vertex(extra);
                core::at(points, 3) = corner(2);
            } else if (core::at(shared, 0) >= 0 && core::at(shared, 2) >= 0) {
                core::at(points, 0) = corner(0);
                core::at(points, 1) = corner(1);
                core::at(points, 2) = corner(2);
                core::at(points, 3) = read_stored_vertex(extra);
            }
            // (otherwise - not possible with two shared corners - face j's corners as they are)
            add_debug_shape(4, points, checksum_);
            break;
        }
    }
}

// 0x00416320
std::span<const uint16_t> TriMeshSource::get_faces_in_box(const maths::BBox* box, std::span<uint16_t> buffer)
{
    if (!bsp_)
        return std::span<const uint16_t>(all_faces()).first(static_cast<size_t>(tri_count_));
    s_face_buffer = buffer;
    s_face_count = 0;
    gather_faces(bsp_, box);
    std::span<const uint16_t> result;
    if (s_face_count < 0)
        result = std::span<const uint16_t>(all_faces()).first(static_cast<size_t>(tri_count_));
    else
        result = std::span<const uint16_t>(s_face_buffer).first(static_cast<size_t>(s_face_count));
    s_face_buffer = {};
    return result;
}

// 0x004160E0 (the less side when the box starts strictly below the split, the greater when it ends at or
// above it; NaN takes neither; too many faces mark the count -1)
void TriMeshSource::gather_faces(const HitTreeNode* node, const maths::BBox* box)
{
    while (!node->is_end_node()) {
        const int axis = node->get_axis();
        auto split = static_cast<float>(node->get_split() * static_cast<double>(kSixteenth));
        if (get_component(&box->min, axis) < split)
            gather_faces(node->less_child(), box);
        if (!(get_component(&box->max, axis) >= split) || s_face_count < 0)
            return;
        node = node->greater_child();
    }
    const uint32_t n = node->leaf_face_count();
    const auto first = static_cast<size_t>(s_face_count);
    s_face_count += static_cast<int>(n);
    if (std::cmp_greater(s_face_count, tri_count_)) {
        s_face_count = -1;
        return;
    }
    const std::span<uint16_t> out = s_face_buffer.subspan(first, n);
    std::ranges::copy(node->leaf_face_run(), out.begin());
}

// 0x004163B0
bool TriMeshSource::is_point_behind_faces(const maths::Vec4* point, int unused)
{
    if (!bsp_)
        return false;
    s_face_count = 0;
    if (!count_faces_behind(bsp_, point, unused))
        return false;
    return std::cmp_equal(s_face_count, tri_count_);
}

// 0x004161D0 (a point at or above a split ends the walk; in a leaf, the count stops at the first face the point
// is in front of)
bool TriMeshSource::count_faces_behind(const HitTreeNode* node, const maths::Vec4* point, int unused)
{
    if (!node->is_end_node()) {
        if (!(node->get_split() * static_cast<double>(kSixteenth) > get_component(point, node->get_axis())))
            return true;
        return count_faces_behind(node->less_child(), point, unused);
    }
    const uint32_t n = node->leaf_face_count();
    for (uint32_t i = 0; i < n; ++i) {
        const int face = core::at(node->leaf_face_run(), i);
        const maths::Vec4 normal = get_triangle_normal(face);
        const maths::Vec4 corner = read_stored_vertex(static_cast<int>(face_vertex(core::at(node->leaf_face_run(), i), 0)));
        auto dx = static_cast<float>(static_cast<double>(point->x) - corner.x);
        const double dy = static_cast<double>(point->y) - corner.y;
        const double dz = static_cast<double>(point->z) - corner.z;
        const double distance = (normal.z * dz + dy * normal.y) + static_cast<double>(dx) * normal.x;
        if (distance > 0.0)
            break;
        ++s_face_count;
    }
    return true;
}

// 0x00414D70
HitTreeNode::HitTreeNode() = default;

// 0x00415C90
int HitTreeNode::tally_nodes() const
{
    if (is_end_node())
        return 1;
    const int greater = greater_child()->tally_nodes();
    return greater + less_child()->tally_nodes() + 1;
}

// 0x00415CE0
int HitTreeNode::count_faces() const
{
    if (is_end_node())
        return leaf_face_count();
    const int greater = greater_child()->count_faces();
    return less_child()->count_faces() + greater;
}

// 0x00415D30
uint16_t* HitTreeNode::first_face_list() const
{
    if (is_end_node())
        return get_leaf_faces();
    uint16_t* less = less_child()->first_face_list();
    uint16_t* greater = greater_child()->first_face_list();
    return less < greater ? less : greater;
}

// 0x00415D80
void HitTreeNode::get_slide(const maths::Vec4* delta)
{
    HitTreeNode* node = this;
    while (!node->is_end_node()) {
        const double at = node->get_split() * static_cast<double>(kSixteenth) + get_component(delta, node->get_axis());
        node->set_split(truncate(at * kSixteen));
        node->less_child()->get_slide(delta);
        node = node->greater_child();
    }
}

// 0x00415E00
void HitTreeNode::rotate_y(const maths::Vec4* centre, int quarter_turns, bool move_to_centre)
{
    if (move_to_centre) {
        const maths::Vec4 back{.x = -centre->x, .y = -centre->y, .z = -centre->z, .w = -centre->w};
        get_slide(&back);
    }
    if (!is_end_node()) {
        const int axis = get_axis();
        if (axis != 1) {
            const int swapped = axis == 0 ? 2 : 0;
            auto negate = [this]() {
                set_split(-get_split());
                chain_link_ ^= 1; // the sides swap
            };
            switch (quarter_turns) {
            case 1:
                if (axis == 0)
                    negate();
                axis_tag_ = (axis_tag_ & ~3) | swapped;
                break;
            case 2:
                negate();
                break;
            case 3:
                if (axis == 2)
                    negate();
                axis_tag_ = (axis_tag_ & ~3) | swapped;
                break;
            default:
                break;
            }
        }
        less_child()->rotate_y(centre, quarter_turns, false);
        greater_child()->rotate_y(centre, quarter_turns, false);
    }
    if (move_to_centre)
        get_slide(centre);
}

// 0x00415F30
void HitTreeNode::scale_tool(const maths::Vec4* centre, const maths::Vec4* scale, bool move_to_centre)
{
    if (move_to_centre) {
        const maths::Vec4 back{.x = -centre->x, .y = -centre->y, .z = -centre->z, .w = -centre->w};
        get_slide(&back);
    }
    if (!is_end_node()) {
        const double at = static_cast<double>(get_split()) * get_component(scale, get_axis()) * kSixteenth;
        set_split(truncate(at * kSixteen));
        less_child()->scale_tool(centre, scale, false);
        greater_child()->scale_tool(centre, scale, false);
    }
    if (move_to_centre)
        get_slide(centre);
}

// 0x00416A10 (a failed allocation crashes, as in the original)
HitTreeNode* HitTreeNode::replicate(int) const
{
    const int nodes = tally_nodes();
    const int faces = count_faces();
    if (nodes <= 0)
        return nullptr;
    const uint16_t* old_faces = first_face_list();
    // new HitTreeNode[nodes]
    HitTreeNode* copy = new_tree_nodes(nodes);
    auto* new_faces = static_cast<uint16_t*>(memory::alloc(faces * 2));
    const std::span<HitTreeNode> copied(copy, static_cast<size_t>(nodes));
    std::copy_n(this, nodes, copy); // (the tree's nodes are one array, this the first)
    std::memcpy(new_faces, old_faces, faces * 2);
    for (HitTreeNode& node : copied) {
        node.chain_link_ = node.is_end_node() ? collision_format::tree_link_moved(node.chain_link_, old_faces, new_faces)
                                              : collision_format::tree_link_moved(node.chain_link_, this, copy);
    }
    return copy;
}

} // namespace render
