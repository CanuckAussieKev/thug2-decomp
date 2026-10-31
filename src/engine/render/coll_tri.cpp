// Collision geometry (TriMeshSource) and its BSP tree (HitTreeNode).

#include "engine/render/coll_tri.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_glue.h"

namespace render {

namespace {

constexpr float kSixteenth = 0.0625f;
constexpr float kSixteen = 16.0f;

// The shared index table every face of a geometry is listed by when there's no tree to narrow them down.
uint16_t g_all_faces[2048]{0xFFFF};
uint16_t* all_faces() { return g_all_faces; }

uint16_t* g_face_buffer;
int g_face_count;
int g_face_buffer_size;

// get_faces_in_box's / is_point_behind_faces' state while walking the tree.
uint16_t*& s_face_buffer = g_face_buffer;
int& s_face_count = g_face_count;
int& s_face_buffer_size = g_face_buffer_size;

// The compiler's float-to-integer conversion (_ftol: toward zero, the low 32 bits of the 64-bit result).
int truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

float get_component(const maths::Vec4* v, int axis) { return (&v->x)[axis]; }

} // namespace

// 0x0040B230
maths::Vec4 TriMeshSource::read_stored_vertex(int index) const
{
    if (compressed_) {
        const uint16_t* v = static_cast<const uint16_t*>(vertices_) + 3 * index;
        return {v[0] * 0.0625f + lower_limit_.x, v[1] * 0.0625f + lower_limit_.y, v[2] * 0.0625f + lower_limit_.z, 0.0f};
    }
    const float* v = static_cast<const float*>(vertices_) + 3 * index;
    return {v[0], v[1], v[2], 0.0f};
}

// 0x0040B2D0 (the uncompressed vertex copied as raw words)
void TriMeshSource::read_stored_vertex(int index, maths::Vec4* out) const
{
    if (compressed_) {
        const uint16_t* v = static_cast<const uint16_t*>(vertices_) + 3 * index;
        out->x = v[0] * 0.0625f + lower_limit_.x;
        out->y = v[1] * 0.0625f + lower_limit_.y;
        out->z = v[2] * 0.0625f + lower_limit_.z;
        out->w = 0.0f;
        return;
    }
    std::memcpy(out, static_cast<const float*>(vertices_) + 3 * index, 12);
    out->w = 0.0f;
}

// 0x00414EE0 (x's offset isn't rounded to a float before it's scaled; y's and z's are)
void TriMeshSource::write_stored_vertex(int index, const maths::Vec4* position)
{
    if (compressed_) {
        double x = static_cast<double>(position->x) - lower_limit_.x;
        float y = static_cast<float>(static_cast<double>(position->y) - lower_limit_.y);
        float z = static_cast<float>(static_cast<double>(position->z) - lower_limit_.z);
        uint16_t* v = static_cast<uint16_t*>(vertices_) + 3 * index;
        v[0] = static_cast<uint16_t>(truncate(x * kSixteen));
        v[1] = static_cast<uint16_t>(truncate(static_cast<double>(y) * kSixteen));
        v[2] = static_cast<uint16_t>(truncate(static_cast<double>(z) * kSixteen));
        return;
    }
    std::memcpy(static_cast<float*>(vertices_) + 3 * index, position, 12);
}

// 0x00414F90
void TriMeshSource::get_stored_vertex_array(maths::Vec4* out) const
{
    for (int i = 0; i < num_vertices_; ++i)
        out[i] = read_stored_vertex(i);
}

// 0x00415C10
void TriMeshSource::choose_raw_verts(const maths::Vec4* positions)
{
    auto* box = reinterpret_cast<maths::BBox*>(&lower_limit_);
    box->min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    box->max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    for (int i = 0; i < num_vertices_; ++i)
        box->append_spot(positions[i]);
    for (int i = 0; i < num_vertices_; ++i)
        write_stored_vertex(i, &positions[i]);
}

// 0x00414D80 (one of the edge's z kept unrounded for the normal's x, as the original's x87 code does)
maths::Vec4 TriMeshSource::get_triangle_normal(int face) const
{
    maths::Vec4 v0 = read_stored_vertex(face_vertex(face, 0));
    maths::Vec4 v1 = read_stored_vertex(face_vertex(face, 1));
    maths::Vec4 v2 = read_stored_vertex(face_vertex(face, 2));
    float ax = static_cast<float>(static_cast<double>(v1.x) - v0.x);
    float ay = static_cast<float>(static_cast<double>(v1.y) - v0.y);
    float az = static_cast<float>(static_cast<double>(v1.z) - v0.z);
    float bx = static_cast<float>(static_cast<double>(v2.x) - v0.x);
    float by = static_cast<float>(static_cast<double>(v2.y) - v0.y);
    double bz_exact = static_cast<double>(v2.z) - v0.z;
    float bz = static_cast<float>(bz_exact);
    double nx = bz_exact * ay - static_cast<double>(by) * az;
    double ny = static_cast<double>(az) * bx - static_cast<double>(bz) * ax;
    double nz = static_cast<double>(by) * ax - static_cast<double>(ay) * bx;
    double length = std::sqrt((nz * nz + ny * ny) + nx * nx);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        nx *= inverse;
        ny *= inverse;
        nz *= inverse;
    }
    return {static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz), 0.0f};
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
    uint32_t vertex_size = num_vertices_ * (compressed_ ? 6u : 12u);
    copy->vertices_ = memory::alloc(vertex_size);
    uint32_t face_size = tri_count_ * (small_faces_ ? 8u : 10u);
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
        memory::Hub::get_pull()->dealloc(reinterpret_cast<uint8_t*>(bsp_) - 4);
    bsp_ = nullptr;
    return true;
}

// 0x004169C0
bool TriMeshSource::relocate(uint32_t, uint32_t vertex_base, uint32_t vertex_byte_base, uint32_t face_base,
                               uint32_t node_base, uint32_t face_list_base)
{
    vertices_ = reinterpret_cast<void*>(reinterpret_cast<uint32_t>(vertices_) + vertex_base);
    vertex_bytes_ = reinterpret_cast<uint8_t*>(reinterpret_cast<uint32_t>(vertex_bytes_) + vertex_byte_base);
    faces_ = reinterpret_cast<uint8_t*>(reinterpret_cast<uint32_t>(faces_) + face_base);
    bsp_ = reinterpret_cast<HitTreeNode*>(reinterpret_cast<uint32_t>(bsp_) + node_base);
    HitTreeNode::relocate(bsp_, node_base, face_list_base);
    return true;
}

// 0x00416400
void TriMeshSource::get_slide(const maths::Vec4* delta)
{
    if (bsp_)
        bsp_->get_slide(delta);
    if (!compressed_) {
        float* v = static_cast<float*>(vertices_);
        for (int i = 0; i < num_vertices_; ++i, v += 3) {
            v[0] = static_cast<float>(static_cast<double>(v[0]) + delta->x);
            v[1] = static_cast<float>(static_cast<double>(v[1]) + delta->y);
            v[2] = static_cast<float>(static_cast<double>(delta->z) + v[2]);
        }
    }
    auto moved = [delta](const maths::Vec4& p) {
        return maths::Vec4{static_cast<float>(static_cast<double>(p.x) + delta->x),
                           static_cast<float>(static_cast<double>(p.y) + delta->y),
                           static_cast<float>(static_cast<double>(p.z) + delta->z),
                           static_cast<float>(static_cast<double>(p.w) + delta->w)};
    };
    maths::Vec4 min = moved(lower_limit_);
    maths::Vec4 max = moved(upper_limit_);
    lower_limit_ = min;
    upper_limit_ = max;
}

// 0x004164F0 (a failed allocation of the temporary array crashes - the vertices are read into null - as in the
// original; the array's element constructor, 0x005902E0, does nothing)
void TriMeshSource::rotate_y(const maths::Vec4* centre, int quarter_turns)
{
    maths::Vec4 back{-centre->x, -centre->y, -centre->z, -centre->w};
    get_slide(&back);
    if (bsp_)
        bsp_->rotate_y(centre, quarter_turns, false);
    maths::Vec4* positions = nullptr;
    if (compressed_) {
        memory::enter_heap_scope(memory::top_down_heap());
        positions = static_cast<maths::Vec4*>(memory::alloc(num_vertices_ * sizeof(maths::Vec4)));
        memory::leave_heap_scope();
        get_stored_vertex_array(positions);
    }
    for (int i = 0; i < num_vertices_; ++i) {
        if (positions) {
            positions[i].turn_quarter_about_y(quarter_turns);
            continue;
        }
        maths::Vec4 v;
        read_stored_vertex(i, &v);
        v.turn_quarter_about_y(quarter_turns);
        float* stored = static_cast<float*>(vertices_) + 3 * i;
        stored[0] = v.x;
        stored[1] = v.y;
        stored[2] = v.z;
    }
    maths::Vec4 min = lower_limit_;
    maths::Vec4 max = upper_limit_;
    min.turn_quarter_about_y(quarter_turns);
    max.turn_quarter_about_y(quarter_turns);
    auto* box = reinterpret_cast<maths::BBox*>(&lower_limit_);
    box->min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    box->max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    box->append_spot(min);
    box->append_spot(max);
    if (positions) {
        for (int i = 0; i < num_vertices_; ++i)
            write_stored_vertex(i, &positions[i]);
        memory::Hub::get_pull()->dealloc(positions);
    }
    get_slide(centre);
    unknown3c_ = 0;
}

// 0x00416730 (a failed allocation crashes, as in RotateY)
void TriMeshSource::scale_tool(const maths::Vec4* centre, const maths::Vec4* scale)
{
    maths::Vec4 back{-centre->x, -centre->y, -centre->z, -centre->w};
    get_slide(&back);
    if (bsp_)
        bsp_->scale_tool(centre, scale, false);
    maths::Vec4* positions = nullptr;
    if (compressed_) {
        memory::enter_heap_scope(memory::top_down_heap());
        positions = static_cast<maths::Vec4*>(memory::alloc(num_vertices_ * sizeof(maths::Vec4)));
        memory::leave_heap_scope();
        get_stored_vertex_array(positions);
    }
    for (int i = 0; i < num_vertices_; ++i) {
        float* v = positions ? &positions[i].x : static_cast<float*>(vertices_) + 3 * i;
        v[0] *= scale->x;
        v[1] *= scale->y;
        v[2] *= scale->z;
    }
    lower_limit_ = {lower_limit_.x * scale->x, lower_limit_.y * scale->y, lower_limit_.z * scale->z, lower_limit_.w};
    upper_limit_ = {upper_limit_.x * scale->x, upper_limit_.y * scale->y, upper_limit_.z * scale->z, upper_limit_.w};
    if (positions) {
        for (int i = 0; i < num_vertices_; ++i)
            write_stored_vertex(i, &positions[i]);
        memory::Hub::get_pull()->dealloc(positions);
    }
    get_slide(centre);
    unknown3c_ = 0;
}

// 0x00414D30
bool TriMeshSource::set_up_all_faces()
{
    uint16_t* table = all_faces();
    if (table[0] != 0) {
        for (int i = 0; i < 0x800; ++i)
            table[i] = static_cast<uint16_t>(i);
    }
    return bsp_ != nullptr;
}

// 0x00414FE0 (the original also reads each face's flags and tests a sign they can't have, and reads the first
// face's corners before each pass; neither has an effect)
void TriMeshSource::add_occlusion_polys()
{
    constexpr float kSameDirection = 0.05f;
    maths::Vec4 points[4];
    for (int i = 0; i < tri_count_ - 1; ++i) {
        for (int j = i + 1; j < tri_count_; ++j) {
            // the corners shared: shared[k] is face i's corner k if face j has it, other[m] face j's corner m if
            // face i has it
            int shared[3] = {-1, -1, -1};
            int other[3] = {-1, -1, -1};
            int matches = 0;
            for (int k = 0; k < 3; ++k) {
                uint16_t corner = static_cast<uint16_t>(face_vertex(i, k));
                for (int m = 0; m < 3; ++m) {
                    if (corner == static_cast<uint16_t>(face_vertex(j, m))) {
                        shared[k] = corner;
                        other[m] = static_cast<int>(face_vertex(j, m));
                        ++matches;
                        break;
                    }
                }
            }
            if (matches != 2)
                continue;
            // both normals ((v1 - v0) x (v2 - v0)), rounded where the original's x87 code stores them
            maths::Vec4 a = read_stored_vertex(face_vertex(i, 0));
            maths::Vec4 b = read_stored_vertex(face_vertex(i, 1));
            maths::Vec4 c = read_stored_vertex(face_vertex(i, 2));
            double cx = static_cast<double>(c.x) - a.x;
            double cy = static_cast<double>(c.y) - a.y;
            double cz = static_cast<double>(c.z) - a.z;
            float bx = static_cast<float>(static_cast<double>(b.x) - a.x);
            float by = static_cast<float>(static_cast<double>(b.y) - a.y);
            double bz = static_cast<double>(b.z) - a.z;
            float n1x = static_cast<float>(by * cz - bz * cy);
            float n1y = static_cast<float>(bz * cx - cz * bx);
            float n1z = static_cast<float>(cy * bx - by * cx);
            maths::Vec4 a2 = read_stored_vertex(face_vertex(j, 0));
            maths::Vec4 b2 = read_stored_vertex(face_vertex(j, 1));
            maths::Vec4 c2 = read_stored_vertex(face_vertex(j, 2));
            points[0] = a2;
            points[1] = b2;
            points[2] = c2;
            auto difference = [](float p, float q) { return static_cast<float>(static_cast<double>(p) - q); };
            float cx2 = difference(c2.x, a2.x), cy2 = difference(c2.y, a2.y), cz2 = difference(c2.z, a2.z);
            float bx2 = difference(b2.x, a2.x), by2 = difference(b2.y, a2.y), bz2 = difference(b2.z, a2.z);
            double n2x = static_cast<double>(by2) * cz2 - static_cast<double>(bz2) * cy2;
            float n2y = static_cast<float>(static_cast<double>(bz2) * cx2 - static_cast<double>(cz2) * bx2);
            float n2z = static_cast<float>(static_cast<double>(cy2) * bx2 - static_cast<double>(by2) * cx2);
            double unit1x = n1x;
            double length1 = std::sqrt((unit1x * n1x + static_cast<double>(n1z) * n1z) + static_cast<double>(n1y) * n1y);
            if (length1 > 0.0) {
                double inverse = 1.0 / length1;
                unit1x = n1x * inverse;
                n1y = static_cast<float>(n1y * inverse);
                n1z = static_cast<float>(n1z * inverse);
            }
            double length2 = std::sqrt((n2x * n2x + static_cast<double>(n2z) * n2z) + static_cast<double>(n2y) * n2y);
            if (length2 > 0.0) {
                double inverse = 1.0 / length2;
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
                if (other[m] == -1) {
                    extra = static_cast<int>(face_vertex(j, m));
                    break;
                }
            }
            auto corner = [this, i](int k) { return read_stored_vertex(face_vertex(i, k)); };
            if (shared[0] >= 0 && shared[1] >= 0) {
                points[0] = corner(0);
                points[1] = read_stored_vertex(extra);
                points[2] = corner(1);
                points[3] = corner(2);
            } else if (shared[0] < 0 && shared[1] >= 0 && shared[2] >= 0) {
                points[0] = corner(0);
                points[1] = corner(1);
                points[2] = read_stored_vertex(extra);
                points[3] = corner(2);
            } else if (shared[0] >= 0 && shared[2] >= 0) {
                points[0] = corner(0);
                points[1] = corner(1);
                points[2] = corner(2);
                points[3] = read_stored_vertex(extra);
            }
            // (otherwise - not possible with two shared corners - face j's corners as they are)
            add_debug_shape(4, points, checksum_);
            break;
        }
    }
}

// 0x00416320
uint16_t* TriMeshSource::get_faces_in_box(const maths::BBox* box, int* count, uint16_t* buffer, int buffer_size)
{
    if (!bsp_) {
        *count = tri_count_;
        return all_faces();
    }
    s_face_buffer = buffer;
    s_face_buffer_size = buffer_size;
    s_face_count = 0;
    gather_faces(bsp_, box);
    uint16_t* result;
    if (s_face_count < 0) {
        *count = tri_count_;
        result = all_faces();
    } else {
        *count = s_face_count;
        result = s_face_buffer;
    }
    s_face_buffer = nullptr;
    s_face_buffer_size = 0;
    return result;
}

// 0x004160E0 (the less side when the box starts strictly below the split, the greater when it ends at or
// above it; NaN takes neither; too many faces mark the count -1)
void TriMeshSource::gather_faces(const HitTreeNode* node, const maths::BBox* box)
{
    while (!node->is_end_node()) {
        int axis = node->get_axis();
        float split = static_cast<float>(node->get_split() * static_cast<double>(kSixteenth));
        if (get_component(&box->min, axis) < split)
            gather_faces(node->less_child(), box);
        if (!(get_component(&box->max, axis) >= split) || s_face_count < 0)
            return;
        node = node->greater_child();
    }
    uint32_t n = node->leaf_face_count();
    uint16_t* out = s_face_buffer + s_face_count;
    s_face_count += n;
    if (s_face_count > tri_count_) {
        s_face_count = -1;
        return;
    }
    const uint16_t* faces = node->get_leaf_faces();
    for (uint32_t i = 0; i < n; ++i)
        out[i] = faces[i];
}

// 0x004163B0
bool TriMeshSource::is_point_behind_faces(const maths::Vec4* point, int unused)
{
    if (!bsp_)
        return false;
    s_face_count = 0;
    if (!count_faces_behind(bsp_, point, unused))
        return false;
    return s_face_count == tri_count_;
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
    uint32_t n = node->leaf_face_count();
    for (uint32_t i = 0; i < n; ++i) {
        int face = node->get_leaf_faces()[i];
        maths::Vec4 normal = get_triangle_normal(face);
        maths::Vec4 corner = read_stored_vertex(face_vertex(node->get_leaf_faces()[i], 0));
        float dx = static_cast<float>(static_cast<double>(point->x) - corner.x);
        double dy = static_cast<double>(point->y) - corner.y;
        double dz = static_cast<double>(point->z) - corner.z;
        double distance = (normal.z * dz + dy * normal.y) + static_cast<double>(dx) * normal.x;
        if (distance > 0.0)
            break;
        ++s_face_count;
    }
    return true;
}

// 0x00414D70
HitTreeNode* HitTreeNode::make_new()
{
    axis_tag_ = 0;
    chain_link_ = 0;
    return this;
}

// 0x00415C90
int HitTreeNode::tally_nodes() const
{
    if (is_end_node())
        return 1;
    int greater = greater_child()->tally_nodes();
    return greater + less_child()->tally_nodes() + 1;
}

// 0x00415CE0
int HitTreeNode::count_faces() const
{
    if (is_end_node())
        return leaf_face_count();
    int greater = greater_child()->count_faces();
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
        double at = node->get_split() * static_cast<double>(kSixteenth) + get_component(delta, node->get_axis());
        node->set_split(truncate(at * kSixteen));
        node->less_child()->get_slide(delta);
        node = node->greater_child();
    }
}

// 0x00415E00
void HitTreeNode::rotate_y(const maths::Vec4* centre, int quarter_turns, bool move_to_centre)
{
    if (move_to_centre) {
        maths::Vec4 back{-centre->x, -centre->y, -centre->z, -centre->w};
        get_slide(&back);
    }
    if (!is_end_node()) {
        int axis = get_axis();
        if (axis != 1) {
            int swapped = axis == 0 ? 2 : 0;
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
        maths::Vec4 back{-centre->x, -centre->y, -centre->z, -centre->w};
        get_slide(&back);
    }
    if (!is_end_node()) {
        double at = static_cast<double>(get_split()) * get_component(scale, get_axis()) * kSixteenth;
        set_split(truncate(at * kSixteen));
        less_child()->scale_tool(centre, scale, false);
        greater_child()->scale_tool(centre, scale, false);
    }
    if (move_to_centre)
        get_slide(centre);
}

// 0x00416000
bool HitTreeNode::relocate(HitTreeNode* node, uint32_t node_base, uint32_t face_base)
{
    if (node->is_end_node()) {
        node->chain_link_ = face_base + node->chain_link_ * 2;
        return true;
    }
    node->chain_link_ = ((node->chain_link_ & ~3u) + node_base) | (node->chain_link_ & 3);
    relocate(node->less_child(), node_base, face_base);
    relocate(node->greater_child(), node_base, face_base);
    return true;
}

// 0x00416A10 (a failed allocation crashes, as in the original)
HitTreeNode* HitTreeNode::replicate(int) const
{
    int nodes = tally_nodes();
    int faces = count_faces();
    if (nodes <= 0)
        return nullptr;
    uint16_t* old_faces = first_face_list();
    // new HitTreeNode[nodes]: the count, then the nodes
    auto* memory = static_cast<uint32_t*>(memory::alloc(nodes * 8 + 4));
    HitTreeNode* copy = nullptr;
    if (memory) {
        *memory = nodes;
        copy = reinterpret_cast<HitTreeNode*>(memory + 1);
        for (int i = 0; i < nodes; ++i)
            copy[i].make_new();
    }
    auto* new_faces = static_cast<uint16_t*>(memory::alloc(faces * 2));
    std::memcpy(copy, this, nodes * 8);
    std::memcpy(new_faces, old_faces, faces * 2);
    uint32_t node_delta = reinterpret_cast<uint32_t>(copy) - reinterpret_cast<uint32_t>(this);
    uint32_t face_delta = reinterpret_cast<uint32_t>(new_faces) - reinterpret_cast<uint32_t>(old_faces);
    for (int i = 0; i < nodes; ++i) {
        HitTreeNode& node = copy[i];
        if (node.is_end_node())
            node.chain_link_ += face_delta;
        else
            node.chain_link_ = ((node.chain_link_ & ~3u) + node_delta) | (node.chain_link_ & 3);
    }
    return copy;
}

} // namespace Nx
