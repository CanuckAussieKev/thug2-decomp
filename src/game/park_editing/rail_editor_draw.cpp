// The rail editor's sectors (rail_editor.h): each point's piece of rail and post - copies of Sk5Ed_RA_Dynamic /
// Sk5Ed_RAp_Dynamic whose vertices are moved to fit - the dotted line to the cursor, the highlight colours, and the
// checks they use (too far, too steep, too sharp a corner).

#include "game/park_editing/rail_editor.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <array>
#include <span>

#include "engine/core/mem.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"
#include "game/park_editing/rail_editor_internal.h"
#include "engine/maths/mat4.h"
#include "engine/render/coll_tri.h"
#include "engine/render/probe_ray.h"
#include "game/park/park_memory.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/engine_geom.h"
#include "engine/core/checked_index.h"
#include "game/park_editing/park_editing_state.h"
#include "engine/maths/vec4.h"

namespace park_editing {

using namespace rail_editor_internal;
using maths::Vec4;

namespace {

constexpr int kGreenShift = 8;                // a packed colour's green and blue
constexpr int kBlueShift = 16;
constexpr float kHalf = 0.5f;
constexpr int kSegmentVertexCount = 10;       // the vertices a rail segment's model copies

constexpr float float_bits(uint32_t bits) noexcept
{
    auto value = std::bit_cast<float>(bits);
    return value;
}

// The source sectors cloned for the rails, posts and the dotted line (red when the segment wouldn't be valid).
constexpr uint32_t kRailSource = 0x66FE99BF;  // Sk5Ed_RA_Dynamic
constexpr uint32_t kPostSource = 0x2756C52D;  // Sk5Ed_RAp_Dynamic
constexpr uint32_t kDottedGreen = 0x5DC3C690; // Sk5Ed_RAdot_Dynamic_Green
constexpr uint32_t kDottedRed = 0x8DDA17D6;   // Sk5Ed_RAdot_Dynamic

// Script values
constexpr uint32_t kMaxJoinAngle = 0xB1459A78;      // RailEditorMaxJoinAngle (degrees)
constexpr uint32_t kCornerLeaveAngle = 0x76C1DA15;  // Rail_Corner_Leave_Angle (degrees)
constexpr uint32_t kMinimumSeparation = 0x8BEA1C03; // RailEditorMinimumPointSeparation
constexpr uint32_t kMaxSlope = 0x9FD761AF;          // RailEditorMaxSlope (degrees)
constexpr uint32_t kPulseRateKey = 0x4551E901;      // RailEditorHighlightFlashRate
constexpr uint32_t kRedLineFlashRate = 0xEEBE4394;  // RailEditorRedLineFlashRate
constexpr uint32_t kHighlightR = 0xAD28B18B, kHighlightG = 0xC0F55560, kHighlightB = 0xB09FA1EF;
constexpr uint32_t kBadR = 0x89B66FB9, kBadG = 0xE46B8B52, kBadB = 0x94017FDD;

constexpr float kAngleScaleToRad = float_bits(0x3C8EFA35); // 0x00645464
constexpr float kMaxSegment = 4000.0f;                 // 0x0064B8F8
constexpr float kMaxReach = 4500.0f;                   // 0x0064B8FC
constexpr float kNearlyVertical = float_bits(0x3F7FFF58); // 0x0064B900 (0.99999)
constexpr float kPostWidth = 2.625f;                   // 0x0064B908 (also the least post height drawn)
constexpr float kGroundProbe = 10000.0f;               // 0x0064B308
constexpr float kFar = 1e6f;                           // 0x0064B904

// The flashing's frame counter (shared by the highlight and the dotted line).
int& flash_counter()
{
    static int rail_flash_counter{};
    return rail_flash_counter;
}
// A zero vector (0x007CCD30): drawing in place (no move).

// The visibility mask for a flashing frame: shown in the first half of every `rate` frames.
uint32_t flash_mask(int rate)
{
    int& counter = flash_counter();
    const uint32_t mask = counter >= rate / 2 ? 0u : 0xFFFFFFFFu;
    if (++counter >= rate)
        counter = 0;
    return mask;
}

// The sector calls (the geometry's virtual functions; its collision is the word at +8).
render::WorldCell* look_up_sector(uint32_t name) { return render::engine::look_up_sector(name); }
render::Geom* fetch_shape(render::WorldCell* sector)
{
    return static_cast<render::Geom*>(sector->geom_handle());
}
int get_vertex_count(render::Geom* geometry) { return static_cast<int>(geometry->fetch_vertex_tally()); }
// (the buffers are made for the geometry's vertex count: the run is of that)
void get_vertices(render::Geom* geometry, Vec4* out) { geometry->get_vertices({out, geometry->fetch_vertex_tally()}); }
void set_vertices(render::Geom* geometry, Vec4* vertices)
{
    geometry->set_vertices({vertices, geometry->fetch_vertex_tally()});
}
render::TriMeshSource* get_hit_shape(render::Geom* geometry)
{
    return static_cast<render::TriMeshSource*>(geometry->hit_triangles());
}
uint16_t collision_vertex_count(const render::TriMeshSource* collision)
{
    return collision->num_vertices();
}
void set_collision_vertices(render::WorldCell* sector, Vec4* vertices)
{
    sector->choose_raw_verts(vertices);
}

// Where a turned x and z go.
struct TurnedXz {
    float* out_x_{};
    double* out_z_{};
};

// A turn about y: the offset (x, z) and the angle.
struct TurnInput {
    double delta_x_{};
    double delta_z_{};
    float angle{};
};

// One point turned about y: x' = x cos + z sin, z' = z cos - x sin from the differences (in double), the sine
// rounded to float, as the original computes it (x' rounded to float, z' kept in double).
void turn_about_y(TurnInput turn_input, TurnedXz turned)
{
    const auto delta_x = turn_input.delta_x_;
    const auto delta_z = turn_input.delta_z_;
    const auto angle = turn_input.angle;
    auto* const out_x = turned.out_x_;
    auto* const out_z = turned.out_z_;
    const double radians = static_cast<double>(angle) * kAngleScaleToRad;
    const double sine = static_cast<float>(std::sin(radians));
    const double cosine = std::cos(radians);
    *out_x = static_cast<float>(delta_x * cosine + delta_z * sine);
    *out_z = delta_z * cosine - delta_x * sine;
}

// Whether sin(angle) is at least `value` (or either isn't a number).
bool sin_at_least(double angle, float value) { return !(std::sin(angle) < value); }

// The clone of a source sector made for the park (its pieces' counts marked dirty, the collision rebuilt).
render::WorldCell* duplicate_scene_sector(uint32_t source)
{
    const uint32_t name = ParkLayout::singleton()->builder()->duplicate_scene_sector(source, 0, 1, 0);
    set_counts_dirty();
    ParkLayout::singleton()->builder()->rebuild_collision();
    return look_up_sector(name);
}

// The bytes of a colour word (red, green, blue; alpha 0) as the highlight writes them.
struct Colour {
    uint8_t r, g, b;
};

[[nodiscard]] inline uint32_t get_word(const Colour& colour)
{
    return colour.r | static_cast<uint32_t>(colour.g) << kGreenShift | static_cast<uint32_t>(colour.b) << kBlueShift;
}

// The template's vertices of the segment as Stretch turns them: the 10 at the start of the piece, then the 10 at its
// end (0x0064... built on the stack by the original).
const std::array<int, 10> kStartVertices = {{1, 9, 3, 10, 14, 19, 0, 13, 2, 16}};
const std::array<int, 10> kEndVertices = {{6, 11, 7, 8, 15, 18, 4, 12, 5, 17}};

} // namespace

// 0x004F3E00 (cdecl)
render::WorldCell* clone_rail_sector(uint32_t source) { return duplicate_scene_sector(source); }

// 0x004F3D80
Vec4* transform_rail_point(Vec4* out, const Vec4* point, const Vec4* origin, const Vec4* position, float angle)
{
    const double dx = static_cast<double>(point->x) - origin->x;
    auto dy = static_cast<float>(static_cast<double>(point->y) - origin->y);
    const double dz = static_cast<double>(point->z) - origin->z;
    float x = 0.0f;
    double z = 0.0;
    turn_about_y({.delta_x_ = dx, .delta_z_ = dz, .angle = angle}, {.out_x_ = &x, .out_z_ = &z});
    // (w: the original adds an unset local - garbage - to the position's; here 0)
    out->x = static_cast<float>(static_cast<double>(x) + position->x);
    out->y = static_cast<float>(static_cast<double>(dy) + position->y);
    out->z = static_cast<float>(static_cast<double>(static_cast<float>(z)) + position->z);
    out->w = static_cast<float>(0.0 + position->w);
    return out;
}

// 0x004F3E40
bool rail_corner_ok(const Vec4* a, const Vec4* b, const Vec4* c)
{
    // The two directions on the ground plane, each normalised by its length rounded to float (y 0, times the
    // inverse); the cosine between them rounded to float.
    // Where a direction's x, y and z go.
    struct DirectionOut {
        double* x{};
        double* y{};
        double* z{};
    };
    auto direction = [](const Vec4* from, const Vec4* to, DirectionOut direction_out) {
        auto* const x = direction_out.x;
        auto* const y = direction_out.y;
        auto* const z = direction_out.z;
        *x = static_cast<double>(to->x) - from->x;
        *z = static_cast<double>(to->z) - from->z;
        *y = 0.0;
        const double exact = std::sqrt(*z * *z + *x * *x);
        auto length = static_cast<float>(exact);
        if (exact > 0.0) {
            auto inverse = static_cast<float>(1.0 / length);
            *x = *x * inverse;
            *y = 0.0 * inverse;
            *z = *z * inverse;
        }
    };
    double x1 = 0.0, y1 = 0.0, z1 = 0.0, x2 = 0.0, y2 = 0.0, z2 = 0.0;
    direction(a, b, {.x = &x1, .y = &y1, .z = &z1});
    direction(b, c, {.x = &x2, .y = &y2, .z = &z2});
    auto dot = static_cast<float>((z2 * z1 + y2 * y1) + x2 * x1);
    return !(std::cos(static_cast<double>(script::fetch_float(kCornerLeaveAngle)) * kAngleScaleToRad) > dot);
}

namespace {
// The distance between two points (the squares summed z, x, then y - as 0x004F3F30 does).
double span_to(const Vec4* a, const Vec4* b)
{
    const double dx = static_cast<double>(a->x) - b->x;
    const double dy = static_cast<double>(a->y) - b->y;
    const double dz = static_cast<double>(a->z) - b->z;
    return std::sqrt((dz * dz + dx * dx) + dy * dy);
}
// The same summed z, y, then x (as move_point does).
double distance_zyx(const Vec4* a, const Vec4* b)
{
    const double dx = static_cast<double>(a->x) - b->x;
    const double dy = static_cast<double>(a->y) - b->y;
    const double dz = static_cast<double>(a->z) - b->z;
    return std::sqrt((dz * dz + dy * dy) + dx * dx);
}
} // namespace

// 0x004F3F30
int rail_too_far(const Vec4* a, const Vec4* b) { return span_to(a, b) > kMaxSegment ? 1 : 0; }

// 0x004F3F70
int rail_out_of_reach(const Vec4* a, const Vec4* b) { return span_to(a, b) > kMaxReach ? 1 : 0; }

// 0x004F3FB0
bool rail_segment_valid(const Vec4* a, const Vec4* b)
{
    const double dx = static_cast<double>(b->x) - a->x;
    const double dy = static_cast<double>(b->y) - a->y;
    const double dz = static_cast<double>(b->z) - a->z;
    if (std::sqrt((dz * dz + dy * dy) + dx * dx) > kMaxSegment)
        return false;
    Vec4 d = {.x = static_cast<float>(dx), .y = static_cast<float>(dy), .z = static_cast<float>(dz),
              .w = static_cast<float>(static_cast<double>(b->w) - a->w)};
    auto length = static_cast<float>(
        std::sqrt((static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y) + static_cast<double>(d.x) * d.x));
    if (script::fetch_float(kMinimumSeparation) > length)
        return false;
    d.make_unit();
    const float slope = d.y < 0.0f ? -d.y : d.y;
    return sin_at_least(static_cast<double>(script::fetch_float(kMaxSlope)) * kAngleScaleToRad, slope);
}

// 0x004F4B90
void orient_rail_vertices(const Vec4* direction, const Vec4* translation, std::span<Vec4> vertices, std::span<const int> indices)
{
    const Vec4 origin = core::at(vertices, core::at(indices, 0));
    float x = direction->x, y = direction->y, z = direction->z;
    const double length =
        std::sqrt((static_cast<double>(z) * z + static_cast<double>(y) * y) + static_cast<double>(x) * x);
    if (length > 0.0) {
        const double inverse = 1.0 / length;
        x = static_cast<float>(x * inverse);
        y = static_cast<float>(y * inverse);
        z = static_cast<float>(z * inverse);
    }
    // A side across the direction on the ground (straight up or down: z), and the up between them.
    Vec4 side;
    float side_w = 0.0f;
    if ((y < 0.0f ? -y : y) > kNearlyVertical) {
        side = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f}; // (its w isn't written: the last row's w is 1)
        side_w = 1.0f;
    } else {
        const double a = static_cast<double>(y) * 0.0f;
        side = {.x = static_cast<float>(z - a),
                .y = static_cast<float>(static_cast<double>(x) * 0.0f - static_cast<double>(z) * 0.0f),
                .z = static_cast<float>(a - x), .w = 0.0f};
        side.make_unit();
        side_w = side.w;
    }
    double ux = static_cast<double>(side.z) * y - static_cast<double>(side.y) * z;
    double uy = static_cast<double>(z) * side.x - static_cast<double>(side.z) * x;
    const double uz = static_cast<double>(side.y) * x - static_cast<double>(y) * side.x;
    auto uz_f = static_cast<float>(uz);
    const double up_length = std::sqrt((uz * uz_f + uy * uy) + ux * ux);
    if (up_length > 0.0) {
        const double inverse = 1.0 / up_length;
        ux *= inverse;
        uy *= inverse;
        uz_f = static_cast<float>(uz_f * inverse);
    }
    maths::Mat4 m;
    core::at(m.row, 0) = {.x = x, .y = y, .z = z, .w = direction->w};
    core::at(m.row, 1) = {.x = static_cast<float>(ux), .y = static_cast<float>(uy), .z = uz_f, .w = 0.0f};
    core::at(m.row, 2) = {.x = side.x, .y = side.y, .z = side.z, .w = side_w};
    // (the original leaves the last row's x, y, z unset - only its w is written; here 0)
    core::at(m.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    for (const int index : indices) {
        Vec4& v = core::at(vertices, index);
        Vec4 offset = {.x = static_cast<float>(static_cast<double>(v.x) - origin.x),
                       .y = static_cast<float>(static_cast<double>(v.y) - origin.y),
                       .z = static_cast<float>(static_cast<double>(v.z) - origin.z),
                       .w = static_cast<float>(static_cast<double>(v.w) - origin.w)};
        Vec4 turned = offset * m;
        v.x = static_cast<float>(static_cast<double>(turned.x) + translation->x);
        v.y = static_cast<float>(static_cast<double>(turned.y) + translation->y);
        v.z = static_cast<float>(static_cast<double>(turned.z) + translation->z);
        v.w = static_cast<float>(static_cast<double>(turned.w) + translation->w);
    }
}

// 0x004F4E70
void stretch_rail_sector(uint32_t source, const Vec4* from, const Vec4* previous, const Vec4* to, render::WorldCell* sector,
                         render::WorldCell* previous_sector, bool update_collision)
{
    render::Geom* source_geometry = fetch_shape(look_up_sector(source));
    const int count = get_vertex_count(source_geometry);
    const uint32_t bytes = static_cast<uint32_t>(count) << 4;
    auto* vertices_block = static_cast<Vec4*>(memory::alloc_block(bytes));
    const std::span<Vec4> vertices(vertices_block, static_cast<size_t>(count));
    get_vertices(source_geometry, vertices_block);
    const double dx = static_cast<double>(to->x) - from->x;
    const double dy = static_cast<double>(to->y) - from->y;
    auto dz = static_cast<float>(static_cast<double>(to->z) - from->z);
    auto dw = static_cast<float>(static_cast<double>(to->w) - from->w);
    if (previous_sector) {
        // Joined on: the start turned halfway between the previous segment's direction and this one's, and the
        // previous segment's end vertices moved onto this one's start.
        const double px = static_cast<double>(from->x) - previous->x;
        auto py = static_cast<float>(static_cast<double>(from->y) - previous->y);
        auto pz = static_cast<float>(static_cast<double>(from->z) - previous->z);
        auto pw = static_cast<float>(static_cast<double>(from->w) - previous->w);
        auto sx = static_cast<float>(px + dx);
        auto sy = static_cast<float>(static_cast<double>(py) + dy);
        const double sz = static_cast<double>(pz) + dz;
        auto sw = static_cast<float>(static_cast<double>(pw) + dw);
        Vec4 halfway = {.x = static_cast<float>(static_cast<double>(sx) * kHalf),
                        .y = static_cast<float>(static_cast<double>(sy) * kHalf), .z = static_cast<float>(sz * kHalf),
                        .w = static_cast<float>(static_cast<double>(sw) * kHalf)};
        orient_rail_vertices(&halfway, from, vertices, kStartVertices);
        render::Geom* previous_geometry = fetch_shape(previous_sector);
        auto* previous_block =
            static_cast<Vec4*>(memory::alloc_block(static_cast<uint32_t>(get_vertex_count(previous_geometry)) << 4));
        const std::span<Vec4> previous_vertices(previous_block, static_cast<size_t>(get_vertex_count(previous_geometry)));
        get_vertices(previous_geometry, previous_block);
        for (int i = 0; i < kSegmentVertexCount; ++i)
            core::at(previous_vertices, core::at(kEndVertices, i)) = core::at(vertices, core::at(kStartVertices, i));
        set_vertices(previous_geometry, previous_block);
        memory::free_malloc(previous_block);
    } else {
        Vec4 direction = {.x = static_cast<float>(dx), .y = static_cast<float>(dy), .z = dz, .w = dw};
        orient_rail_vertices(&direction, from, vertices, kStartVertices);
    }
    Vec4 direction = {.x = static_cast<float>(static_cast<double>(to->x) - from->x),
                      .y = static_cast<float>(static_cast<double>(to->y) - from->y),
                      .z = static_cast<float>(static_cast<double>(to->z) - from->z),
                      .w = static_cast<float>(static_cast<double>(to->w) - from->w)};
    orient_rail_vertices(&direction, to, vertices, kEndVertices);
    set_vertices(fetch_shape(sector), vertices_block);
    if (update_collision) {
        // The collision's vertices moved as the drawn ones were: each found among the source geometry's (as read
        // back now) by its position - or by x and z alone, keeping its height over that vertex.
        auto* before_block = static_cast<Vec4*>(memory::alloc_block(bytes));
        const std::span<Vec4> before(before_block, static_cast<size_t>(count));
        get_vertices(source_geometry, before_block);
        const render::TriMeshSource* collision = get_hit_shape(source_geometry);
        const int collision_count = collision_vertex_count(collision);
        auto* raw_block = static_cast<Vec4*>(memory::alloc_block(static_cast<uint32_t>(collision_count) << 4));
        const std::span<Vec4> raw(raw_block, static_cast<size_t>(collision_count));
        collision->get_stored_vertex_array(raw);
        for (int v = 0; v < collision_count; ++v) {
            Vec4 vertex = core::at(raw, v);
            bool found = false;
            for (int i = 0; i < count && !found; ++i) {
                if (vertex.x == core::at(before, i).x && vertex.y == core::at(before, i).y && vertex.z == core::at(before, i).z) {
                    core::at(raw, v) = core::at(vertices, i);
                    found = true;
                }
            }
            for (int i = 0; i < count && !found; ++i) {
                if (vertex.x == core::at(before, i).x && vertex.z == core::at(before, i).z) {
                    const double above = static_cast<double>(vertex.y) - core::at(before, i).y;
                    core::at(raw, v) = core::at(vertices, i);
                    core::at(raw, v).y = static_cast<float>(above + core::at(vertices, i).y);
                    found = true;
                }
            }
            if (!found)
                vertex.echo_out();
        }
        set_collision_vertices(sector, raw_block);
        memory::free_malloc(raw_block);
        memory::free_malloc(before_block);
        ParkLayout::singleton()->builder()->rebuild_collision();
    }
    memory::free_malloc(vertices_block);
}

// --- CEditedRailPoint ---------------------------------------------------------------------------------------------

// 0x004F5460
void EditedRailPoint::draw_segment(const Vec4* origin, const Vec4* position, float angle)
{
    if (!following_) {
        delete_segment();
        return;
    }
    if (!segment_)
        segment_ = clone_rail_sector(kRailSource);
    const EditedRailPoint* next = following_;
    if (next->following_) {
        // Joined on to the previous segment unless the turn is sharper than RailEditorMaxJoinAngle.
        const EditedRailPoint* next_next = next->following_;
        Vec4 before = {.x = static_cast<float>(static_cast<double>(next->at_point_.x) - next_next->at_point_.x),
                       .y = static_cast<float>(static_cast<double>(next->at_point_.y) - next_next->at_point_.y),
                       .z = static_cast<float>(static_cast<double>(next->at_point_.z) - next_next->at_point_.z),
                       .w = static_cast<float>(static_cast<double>(next->at_point_.w) - next_next->at_point_.w)};
        Vec4 here = {.x = static_cast<float>(static_cast<double>(at_point_.x) - next->at_point_.x),
                     .y = static_cast<float>(static_cast<double>(at_point_.y) - next->at_point_.y),
                     .z = static_cast<float>(static_cast<double>(at_point_.z) - next->at_point_.z),
                     .w = static_cast<float>(static_cast<double>(at_point_.w) - next->at_point_.w)};
        before.make_unit();
        here.make_unit();
        auto dot =
            static_cast<float>((static_cast<double>(here.z) * before.z + static_cast<double>(here.y) * before.y) +
                               static_cast<double>(here.x) * before.x);
        const bool join = !(std::cos(static_cast<double>(script::fetch_float(kMaxJoinAngle)) * kAngleScaleToRad) > dot);
        Vec4 c, b, a;
        transform_rail_point(&c, &next_next->at_point_, origin, position, angle);
        transform_rail_point(&b, &next->at_point_, origin, position, angle);
        transform_rail_point(&a, &at_point_, origin, position, angle);
        stretch_rail_sector(kRailSource, &b, &c, &a, segment_, join ? next->segment_ : nullptr, true);
        return;
    }
    Vec4 b, a;
    transform_rail_point(&b, &next->at_point_, origin, position, angle);
    transform_rail_point(&a, &at_point_, origin, position, angle);
    // (the original passes an unset local as the previous point: unread without a previous sector)
    stretch_rail_sector(kRailSource, &b, &b, &a, segment_, nullptr, true);
}

// 0x004F56D0
void EditedRailPoint::draw_post(const Vec4* origin, const Vec4* position, float angle)
{
    if (!has_post_ || level_y_ < kPostWidth) {
        delete_post();
        return;
    }
    render::Geom* geometry = fetch_shape(look_up_sector(kPostSource));
    const int count = get_vertex_count(geometry);
    auto* vertices_block = static_cast<Vec4*>(memory::alloc_block(static_cast<uint32_t>(count) << 4));
    const std::span<Vec4> vertices(vertices_block, static_cast<size_t>(count));
    get_vertices(geometry, vertices_block);
    float low = kFar, high = -kFar;
    for (int i = 0; i < count; ++i) {
        const float y = core::at(vertices, i).y;
        low = std::min(low, y);
        high = std::max(high, y);
    }
    // Leaning along the rail at an inner point: the post narrower as the rail climbs.
    float width = kPostWidth;
    if (following_ && preceding_) {
        const double ax = static_cast<double>(preceding_->at_point_.x) - at_point_.x;
        const double ay = static_cast<double>(preceding_->at_point_.y) - at_point_.y;
        auto az = static_cast<float>(static_cast<double>(preceding_->at_point_.z) - at_point_.z);
        auto aw = static_cast<float>(static_cast<double>(preceding_->at_point_.w) - at_point_.w);
        const double bx = static_cast<double>(at_point_.x) - following_->at_point_.x;
        auto by = static_cast<float>(static_cast<double>(at_point_.y) - following_->at_point_.y);
        auto bz = static_cast<float>(static_cast<double>(at_point_.z) - following_->at_point_.z);
        auto bw = static_cast<float>(static_cast<double>(at_point_.w) - following_->at_point_.w);
        auto sx = static_cast<float>(bx + ax);
        const double sy = static_cast<double>(by) + ay;
        const double sz = static_cast<double>(bz) + az;
        auto sw = static_cast<float>(static_cast<double>(bw) + aw);
        Vec4 along = {.x = static_cast<float>(static_cast<double>(sx) * kHalf), .y = static_cast<float>(sy * kHalf),
                      .z = static_cast<float>(sz * kHalf), .w = static_cast<float>(static_cast<double>(sw) * kHalf)};
        along.make_unit();
        width = static_cast<float>(std::sqrt(1.0f - static_cast<double>(along.y) * along.y) * kPostWidth);
    }
    Vec4 at;
    transform_rail_point(&at, &at_point_, origin, position, angle);
    const double drop = static_cast<double>(width) + high;
    for (int i = 0; i < count; ++i) {
        Vec4& v = core::at(vertices, i);
        v.x = static_cast<float>(static_cast<double>(at.x) + v.x);
        v.y = static_cast<float>(static_cast<double>(at.y) + v.y);
        v.z = static_cast<float>(static_cast<double>(at.z) + v.z);
        v.w = static_cast<float>(static_cast<double>(at.w) + v.w);
        v.y = static_cast<float>(static_cast<double>(v.y) - drop);
    }
    // The foot's vertices down to the ground.
    const double foot = (static_cast<double>(level_y_) - drop) - high;
    for (const int i : {8, 9, 10, 11, 1, 2, 5, 6})
        core::at(vertices, i).y = static_cast<float>(static_cast<double>(core::at(vertices, i).y) - foot);
    if (following_) {
        // Turned to face along the rail.
        double dx = static_cast<double>(at_point_.x) - following_->at_point_.x;
        double dz = static_cast<double>(at_point_.z) - following_->at_point_.z;
        const double length = std::sqrt(dz * dz + dx * dx);
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            dx *= inverse;
            dz *= inverse;
        }
        auto minus_z = static_cast<float>(-dz);
        for (int i = 0; i < count; ++i) {
            Vec4& v = core::at(vertices, i);
            const double rx = static_cast<double>(v.x) - at.x;
            const double rz = static_cast<double>(v.z) - at.z;
            v.x = static_cast<float>((dx * rx + rz * minus_z) + at.x);
            v.z = static_cast<float>((dz * rx + dx * rz) + at.z);
        }
    }
    if (!post_)
        post_ = duplicate_scene_sector(kPostSource);
    set_vertices(fetch_shape(post_), vertices_block);
    memory::free_malloc(vertices_block);
}

// 0x004F5D10
float EditedRailPoint::ground_height(const Vec4* position)
{
    render::ProbeRay feeler;
    Vec4 start = {.x = position->x, .y = static_cast<float>(static_cast<double>(position->y) + kGroundProbe), .z = position->z,
                  .w = position->w};
    feeler.change_origin(&start);
    Vec4 end = {.x = position->x, .y = static_cast<float>(static_cast<double>(position->y) - kGroundProbe), .z = position->z,
                .w = position->w};
    feeler.change_finish(&end);
    if (feeler.get_hit_shape(true, false)) {
        const float height = feeler.contact_pos().y;
        feeler.entity().get_release();
        return height;
    }
    feeler.entity().get_release();
    return 0.0f;
}

// 0x004F5E30
void EditedRailPoint::spotlight(bool flash, bool include_end_posts)
{
    if (highlighted_)
        return;
    const uint32_t mask = flash ? flash_mask(script::fetch_int_value(kPulseRateKey)) : 0xFFFFFFFFu;
    const Colour normal = {.r = static_cast<uint8_t>(script::fetch_int_value(kHighlightR)),
                           .g = static_cast<uint8_t>(script::fetch_int_value(kHighlightG)),
                           .b = static_cast<uint8_t>(script::fetch_int_value(kHighlightB))};
    const Colour bad = {.r = static_cast<uint8_t>(script::fetch_int_value(kBadR)),
                        .g = static_cast<uint8_t>(script::fetch_int_value(kBadG)),
                        .b = static_cast<uint8_t>(script::fetch_int_value(kBadB))};
    // Too long a segment either side: everything in the bad colour.
    bool too_long = false;
    if (following_ && rail_too_far(&at_point_, &following_->at_point_))
        too_long = true;
    if (preceding_ && rail_too_far(&at_point_, &preceding_->at_point_))
        too_long = true;
    auto corner_ok = [](const EditedRailPoint* p) {
        return !p->following_ || !p->preceding_ ||
               rail_corner_ok(&p->following_->at_point_, &p->at_point_, &p->preceding_->at_point_);
    };
    auto paint = [mask](render::WorldCell* sector, const Colour& colour) {
        set_sector_colour(sector, get_word(colour));
        set_sector_visibility(sector, mask);
    };
    // (the colour last chosen carries on to the segment when there's no next point to judge it by)
    Colour colour = normal;
    if (post_) {
        colour = corner_ok(this) && !too_long ? normal : bad;
        paint(post_, colour);
    }
    if (segment_) {
        if (following_)
            colour = corner_ok(following_) && corner_ok(this) && !too_long ? normal : bad;
        paint(segment_, colour);
    }
    if (preceding_ && preceding_->segment_)
        paint(preceding_->segment_, corner_ok(preceding_) && corner_ok(this) && !too_long ? normal : bad);
    if (include_end_posts) {
        if (following_ && !following_->following_ && following_->post_)
            paint(following_->post_, normal);
        if (preceding_ && !preceding_->preceding_ && preceding_->post_)
            paint(preceding_->post_, normal);
    }
    highlighted_ = true;
}

// --- CEditedRail --------------------------------------------------------------------------------------------------

// 0x004F4370
void EditedRail::xform(const Vec4* origin, const Vec4* position, float angle)
{
    // (transform_rail_point's turn, but z' not rounded to float before the position is added)
    for (EditedRailPoint* p = point_chain_; p; p = p->following()) {
        const double dx = static_cast<double>(p->at_point().x) - origin->x;
        auto dy = static_cast<float>(static_cast<double>(p->at_point().y) - origin->y);
        const double dz = static_cast<double>(p->at_point().z) - origin->z;
        float x = 0.0f;
        double z = 0.0;
        turn_about_y({.delta_x_ = dx, .delta_z_ = dz, .angle = angle}, {.out_x_ = &x, .out_z_ = &z});
        p->at_point().z = static_cast<float>(z + position->z);
        p->at_point().w = static_cast<float>(0.0 + position->w); // (the original adds an unset local - here 0)
        p->at_point().x = static_cast<float>(static_cast<double>(x) + position->x);
        p->at_point().y = static_cast<float>(static_cast<double>(dy) + position->y);
    }
}

// 0x004F61C0
void EditedRail::draw_segments(const Vec4* origin, const Vec4* position, float angle)
{
    EditedRailPoint* last = nullptr;
    for (EditedRailPoint* p = point_chain_; p; p = p->following())
        last = p;
    for (EditedRailPoint* p = last; p; p = p->preceding())
        p->draw_segment(origin, position, angle);
}

// 0x004F6210
void EditedRail::draw_posts(const Vec4* origin, const Vec4* position, float angle)
{
    for (EditedRailPoint* p = point_chain_; p; p = p->following())
        p->draw_post(origin, position, angle);
}

// 0x004F63B0
bool EditedRail::move_point(int index, const Vec4* position, bool /*updateSuperSectors*/) const
{
    EditedRailPoint* p = fetch_vertex_at(index);
    bool valid = true;
    if (p->following() && !rail_segment_valid(&p->following()->at_point(), position))
        valid = false;
    if (p->preceding() && !rail_segment_valid(&p->preceding()->at_point(), position))
        valid = false;
    if (!ParkBuilderMode::is_inside_park(*position, kRailPlacementMargin))
        valid = false;
    // Moved (even when not valid) unless a neighbour would be out of reach.
    if (p->following() && distance_zyx(position, &p->following()->at_point()) > kMaxReach)
        return valid;
    if (p->preceding() && distance_zyx(position, &p->preceding()->at_point()) > kMaxReach)
        return valid;
    p->at_point() = *position;
    p->set_level_y(static_cast<float>(static_cast<double>(p->at_point().y) - EditedRailPoint::ground_height(&p->at_point())));
    p->draw_post(&park_editing_state().rail_origin(), &park_editing_state().rail_origin(), 0.0f);
    p->draw_segment(&park_editing_state().rail_origin(), &park_editing_state().rail_origin(), 0.0f);
    if (p->preceding())
        p->preceding()->draw_segment(&park_editing_state().rail_origin(), &park_editing_state().rail_origin(), 0.0f);
    return valid;
}

// --- RailAuthoring -----------------------------------------------------------------------------------------

// 0x004F6510
void RailAuthoring::restore_heights()
{
    change_live(false);
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        for (EditedRailPoint* p = rail->point_chain(); p; p = p->following()) {
            if (p->has_post())
                p->at_point().y = static_cast<float>(static_cast<double>(EditedRailPoint::ground_height(&p->at_point())) +
                                                    p->level_y());
        }
    }
    change_live(true);
}

// 0x004F6560
void RailAuthoring::store_heights()
{
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        for (EditedRailPoint* p = rail->point_chain(); p; p = p->following())
            p->set_level_y(static_cast<float>(static_cast<double>(p->at_point().y) - EditedRailPoint::ground_height(&p->at_point())));
    }
}

// 0x004F65A0
void RailAuthoring::draw_all_segments()
{
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following())
        rail->draw_segments(&park_editing_state().rail_origin(), &park_editing_state().rail_origin(), 0.0f);
}

// 0x004F6600
void RailAuthoring::draw_all_posts()
{
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following())
        rail->draw_posts(&park_editing_state().rail_origin(), &park_editing_state().rail_origin(), 0.0f);
}

// 0x004F6650
void RailAuthoring::reassemble()
{
    ParkLayout::singleton()->builder()->rebuild_collision();
    store_heights();
    draw_all_segments();
    draw_all_posts();
}

// 0x004F6AF0
void RailAuthoring::draw_dotted_line(const Vec4* position)
{
    if (!active_node_)
        return;
    const EditedRailPoint* head = active_node_->point_chain();
    if (!head)
        return;
    uint32_t source = kDottedGreen;
    if (rail_too_far(&head->at_point(), position)) {
        if (rail_out_of_reach(&head->at_point(), position)) {
            delete_dotted_line();
            return;
        }
        source = kDottedRed;
    } else if (head->following() && !rail_corner_ok(&head->following()->at_point(), &head->at_point(), position)) {
        source = kDottedRed;
    }
    if (!dotted_line_ || source != dotted_line_name_) {
        delete_dotted_line();
        dotted_line_ = clone_rail_sector(source);
        dotted_line_name_ = source;
    }
    // (the original passes an unset local as the previous point: unread without a previous sector)
    stretch_rail_sector(dotted_line_name_, &head->at_point(), position, position, dotted_line_, nullptr, false);
    if (source == kDottedRed)
        set_sector_visibility(dotted_line_, flash_mask(script::fetch_int_value(kRedLineFlashRate)));
}

} // namespace park_editing
