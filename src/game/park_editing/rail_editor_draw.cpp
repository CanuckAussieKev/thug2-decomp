// The rail editor's sectors (rail_editor.h): each point's piece of rail and post - copies of Sk5Ed_RA_Dynamic /
// Sk5Ed_RAp_Dynamic whose vertices are moved to fit - the dotted line to the cursor, the highlight colours, and the
// checks they use (too far, too steep, too sharp a corner).

#include "game/park_editing/rail_editor.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

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

namespace park_editing {

using namespace rail_editor_internal;
using maths::Vec4;

static int g_rail_flash_counter;

namespace {

float float_bits(uint32_t bits)
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

const float kAngleScaleToRad = float_bits(0x3C8EFA35); // 0x00645464
constexpr float kMaxSegment = 4000.0f;                 // 0x0064B8F8
constexpr float kMaxReach = 4500.0f;                   // 0x0064B8FC
const float kNearlyVertical = float_bits(0x3F7FFF58);  // 0x0064B900 (0.99999)
constexpr float kPostWidth = 2.625f;                   // 0x0064B908 (also the least post height drawn)
constexpr float kGroundProbe = 10000.0f;               // 0x0064B308
constexpr float kFar = 1e6f;                           // 0x0064B904

// The flashing's frame counter (shared by the highlight and the dotted line).
int& flash_counter() { return g_rail_flash_counter; }
// A zero vector (0x007CCD30): drawing in place (no move).
const Vec4& get_zero() { return g_rail_origin; }

// The visibility mask for a flashing frame: shown in the first half of every `rate` frames.
uint32_t flash_mask(int rate)
{
    int& counter = flash_counter();
    uint32_t mask = counter >= rate / 2 ? 0u : 0xFFFFFFFFu;
    if (++counter >= rate)
        counter = 0;
    return mask;
}

// The sector calls (the geometry's virtual functions; its collision is the word at +8).
render::Geom* as_geom(uint8_t* geometry) { return reinterpret_cast<render::Geom*>(geometry); }
uint8_t* look_up_sector(uint32_t name) { return static_cast<uint8_t*>(render::engine::look_up_sector(name)); }
uint8_t* fetch_shape(uint8_t* sector)
{
    return static_cast<uint8_t*>(reinterpret_cast<render::WorldCell*>(sector)->geom_handle_);
}
int get_vertex_count(uint8_t* geometry) { return static_cast<int>(as_geom(geometry)->fetch_vertex_tally()); }
void get_vertices(uint8_t* geometry, Vec4* out) { as_geom(geometry)->get_vertices(out); }
void set_vertices(uint8_t* geometry, Vec4* vertices) { as_geom(geometry)->set_vertices(vertices); }
render::TriMeshSource* get_hit_shape(uint8_t* geometry)
{
    return static_cast<render::TriMeshSource*>(reinterpret_cast<render::Geom*>(geometry)->hit_triangles_);
}
uint16_t collision_vertex_count(const render::TriMeshSource* collision)
{
    return *reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(collision) + 6);
}
void set_collision_vertices(uint8_t* sector, Vec4* vertices)
{
    reinterpret_cast<render::WorldCell*>(sector)->choose_raw_verts(vertices);
}

// One point turned about y: x' = x cos + z sin, z' = z cos - x sin from the differences (in double), the sine
// rounded to float, as the original computes it (x' rounded to float, z' kept in double).
void turn_about_y(double delta_x, double delta_z, float angle, float* out_x, double* out_z)
{
    const double radians = static_cast<double>(angle) * kAngleScaleToRad;
    const double sine = static_cast<float>(std::sin(radians));
    const double cosine = std::cos(radians);
    *out_x = static_cast<float>(delta_x * cosine + delta_z * sine);
    *out_z = delta_z * cosine - delta_x * sine;
}

// Whether sin(angle) is at least `value` (or either isn't a number).
bool sin_at_least(double angle, float value) { return !(std::sin(angle) < value); }

// The clone of a source sector made for the park (its pieces' counts marked dirty, the collision rebuilt).
uint8_t* duplicate_scene_sector(uint32_t source)
{
    uint32_t name = ParkLayout::singleton()->builder_->duplicate_scene_sector(source, 0, 1, 0);
    set_counts_dirty();
    ParkLayout::singleton()->builder_->rebuild_collision();
    return look_up_sector(name);
}

// The bytes of a colour word (red, green, blue; alpha 0) as the highlight writes them.
struct Colour {
    uint8_t r, g, b;
    uint32_t get_word() const { return r | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b) << 16; }
};

// The template's vertices of the segment as Stretch turns them: the 10 at the start of the piece, then the 10 at its
// end (0x0064... built on the stack by the original).
const int kStartVertices[10] = {1, 9, 3, 10, 14, 19, 0, 13, 2, 16};
const int kEndVertices[10] = {6, 11, 7, 8, 15, 18, 4, 12, 5, 17};

} // namespace

// 0x004F3E00 (cdecl)
uint8_t* clone_rail_sector(uint32_t source) { return duplicate_scene_sector(source); }

// 0x004F3D80
Vec4* transform_rail_point(Vec4* out, const Vec4* point, const Vec4* origin, const Vec4* position, float angle)
{
    double dx = static_cast<double>(point->x) - origin->x;
    auto dy = static_cast<float>(static_cast<double>(point->y) - origin->y);
    double dz = static_cast<double>(point->z) - origin->z;
    float x;
    double z;
    turn_about_y(dx, dz, angle, &x, &z);
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
    auto direction = [](const Vec4* from, const Vec4* to, double* x, double* y, double* z) {
        *x = static_cast<double>(to->x) - from->x;
        *z = static_cast<double>(to->z) - from->z;
        *y = 0.0;
        double exact = std::sqrt(*z * *z + *x * *x);
        auto length = static_cast<float>(exact);
        if (exact > 0.0) {
            auto inverse = static_cast<float>(1.0 / length);
            *x = *x * inverse;
            *y = 0.0 * inverse;
            *z = *z * inverse;
        }
    };
    double x1, y1, z1, x2, y2, z2;
    direction(a, b, &x1, &y1, &z1);
    direction(b, c, &x2, &y2, &z2);
    auto dot = static_cast<float>((z2 * z1 + y2 * y1) + x2 * x1);
    return !(std::cos(static_cast<double>(script::fetch_float(kCornerLeaveAngle)) * kAngleScaleToRad) > dot);
}

namespace {
// The distance between two points (the squares summed z, x, then y - as 0x004F3F30 does).
double span_to(const Vec4* a, const Vec4* b)
{
    double dx = static_cast<double>(a->x) - b->x;
    double dy = static_cast<double>(a->y) - b->y;
    double dz = static_cast<double>(a->z) - b->z;
    return std::sqrt((dz * dz + dx * dx) + dy * dy);
}
// The same summed z, y, then x (as move_point does).
double distance_zyx(const Vec4* a, const Vec4* b)
{
    double dx = static_cast<double>(a->x) - b->x;
    double dy = static_cast<double>(a->y) - b->y;
    double dz = static_cast<double>(a->z) - b->z;
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
    double dx = static_cast<double>(b->x) - a->x;
    double dy = static_cast<double>(b->y) - a->y;
    double dz = static_cast<double>(b->z) - a->z;
    if (std::sqrt((dz * dz + dy * dy) + dx * dx) > kMaxSegment)
        return false;
    Vec4 d = {static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz),
              static_cast<float>(static_cast<double>(b->w) - a->w)};
    auto length = static_cast<float>(
        std::sqrt((static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y) + static_cast<double>(d.x) * d.x));
    if (script::fetch_float(kMinimumSeparation) > length)
        return false;
    d.make_unit();
    float slope = d.y < 0.0f ? -d.y : d.y;
    return sin_at_least(static_cast<double>(script::fetch_float(kMaxSlope)) * kAngleScaleToRad, slope);
}

// 0x004F4B90
void orient_rail_vertices(const Vec4* direction, const Vec4* translation, Vec4* vertices, const int* indices, int count)
{
    const Vec4 origin = vertices[indices[0]];
    float x = direction->x, y = direction->y, z = direction->z;
    double length = std::sqrt((static_cast<double>(z) * z + static_cast<double>(y) * y) + static_cast<double>(x) * x);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        x = static_cast<float>(x * inverse);
        y = static_cast<float>(y * inverse);
        z = static_cast<float>(z * inverse);
    }
    // A side across the direction on the ground (straight up or down: z), and the up between them.
    Vec4 side;
    float side_w;
    if ((y < 0.0f ? -y : y) > kNearlyVertical) {
        side = {0.0f, 0.0f, 1.0f, 0.0f}; // (its w isn't written: the last row's w is 1)
        side_w = 1.0f;
    } else {
        double a = static_cast<double>(y) * 0.0f;
        side = {static_cast<float>(z - a),
                static_cast<float>(static_cast<double>(x) * 0.0f - static_cast<double>(z) * 0.0f),
                static_cast<float>(a - x), 0.0f};
        side.make_unit();
        side_w = side.w;
    }
    double ux = static_cast<double>(side.z) * y - static_cast<double>(side.y) * z;
    double uy = static_cast<double>(z) * side.x - static_cast<double>(side.z) * x;
    double uz = static_cast<double>(side.y) * x - static_cast<double>(y) * side.x;
    auto uz_f = static_cast<float>(uz);
    double up_length = std::sqrt((uz * uz_f + uy * uy) + ux * ux);
    if (up_length > 0.0) {
        double inverse = 1.0 / up_length;
        ux *= inverse;
        uy *= inverse;
        uz_f = static_cast<float>(uz_f * inverse);
    }
    maths::Mat4 m;
    m.row[0] = {x, y, z, direction->w};
    m.row[1] = {static_cast<float>(ux), static_cast<float>(uy), uz_f, 0.0f};
    m.row[2] = {side.x, side.y, side.z, side_w};
    // (the original leaves the last row's x, y, z unset - only its w is written; here 0)
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < count; ++i) {
        Vec4& v = vertices[indices[i]];
        Vec4 offset = {static_cast<float>(static_cast<double>(v.x) - origin.x),
                       static_cast<float>(static_cast<double>(v.y) - origin.y),
                       static_cast<float>(static_cast<double>(v.z) - origin.z),
                       static_cast<float>(static_cast<double>(v.w) - origin.w)};
        Vec4 turned = offset * m;
        v.x = static_cast<float>(static_cast<double>(turned.x) + translation->x);
        v.y = static_cast<float>(static_cast<double>(turned.y) + translation->y);
        v.z = static_cast<float>(static_cast<double>(turned.z) + translation->z);
        v.w = static_cast<float>(static_cast<double>(turned.w) + translation->w);
    }
}

// 0x004F4E70
void stretch_rail_sector(uint32_t source, const Vec4* from, const Vec4* previous, const Vec4* to, uint8_t* sector,
                         uint8_t* previous_sector, bool update_collision)
{
    uint8_t* source_geometry = fetch_shape(look_up_sector(source));
    int count = get_vertex_count(source_geometry);
    uint32_t bytes = static_cast<uint32_t>(count) << 4;
    auto* vertices = static_cast<Vec4*>(memory::alloc_block(bytes));
    get_vertices(source_geometry, vertices);
    double dx = static_cast<double>(to->x) - from->x;
    double dy = static_cast<double>(to->y) - from->y;
    auto dz = static_cast<float>(static_cast<double>(to->z) - from->z);
    auto dw = static_cast<float>(static_cast<double>(to->w) - from->w);
    if (previous_sector) {
        // Joined on: the start turned halfway between the previous segment's direction and this one's, and the
        // previous segment's end vertices moved onto this one's start.
        double px = static_cast<double>(from->x) - previous->x;
        auto py = static_cast<float>(static_cast<double>(from->y) - previous->y);
        auto pz = static_cast<float>(static_cast<double>(from->z) - previous->z);
        auto pw = static_cast<float>(static_cast<double>(from->w) - previous->w);
        auto sx = static_cast<float>(px + dx);
        auto sy = static_cast<float>(static_cast<double>(py) + dy);
        double sz = static_cast<double>(pz) + dz;
        auto sw = static_cast<float>(static_cast<double>(pw) + dw);
        Vec4 halfway = {static_cast<float>(static_cast<double>(sx) * 0.5f),
                        static_cast<float>(static_cast<double>(sy) * 0.5f), static_cast<float>(sz * 0.5f),
                        static_cast<float>(static_cast<double>(sw) * 0.5f)};
        orient_rail_vertices(&halfway, from, vertices, kStartVertices, 10);
        uint8_t* previous_geometry = fetch_shape(previous_sector);
        auto* previous_vertices =
            static_cast<Vec4*>(memory::alloc_block(static_cast<uint32_t>(get_vertex_count(previous_geometry)) << 4));
        get_vertices(previous_geometry, previous_vertices);
        for (int i = 0; i < 10; ++i)
            previous_vertices[kEndVertices[i]] = vertices[kStartVertices[i]];
        set_vertices(previous_geometry, previous_vertices);
        memory::free_malloc(previous_vertices);
    } else {
        Vec4 direction = {static_cast<float>(dx), static_cast<float>(dy), dz, dw};
        orient_rail_vertices(&direction, from, vertices, kStartVertices, 10);
    }
    Vec4 direction = {static_cast<float>(static_cast<double>(to->x) - from->x),
                      static_cast<float>(static_cast<double>(to->y) - from->y),
                      static_cast<float>(static_cast<double>(to->z) - from->z),
                      static_cast<float>(static_cast<double>(to->w) - from->w)};
    orient_rail_vertices(&direction, to, vertices, kEndVertices, 10);
    set_vertices(fetch_shape(sector), vertices);
    if (update_collision) {
        // The collision's vertices moved as the drawn ones were: each found among the source geometry's (as read
        // back now) by its position - or by x and z alone, keeping its height over that vertex.
        auto* before = static_cast<Vec4*>(memory::alloc_block(bytes));
        get_vertices(source_geometry, before);
        render::TriMeshSource* collision = get_hit_shape(source_geometry);
        int collision_count = collision_vertex_count(collision);
        auto* raw = static_cast<Vec4*>(memory::alloc_block(static_cast<uint32_t>(collision_count) << 4));
        collision->get_stored_vertex_array(raw);
        for (int v = 0; v < collision_count; ++v) {
            Vec4 vertex = raw[v];
            bool found = false;
            for (int i = 0; i < count && !found; ++i) {
                if (vertex.x == before[i].x && vertex.y == before[i].y && vertex.z == before[i].z) {
                    raw[v] = vertices[i];
                    found = true;
                }
            }
            for (int i = 0; i < count && !found; ++i) {
                if (vertex.x == before[i].x && vertex.z == before[i].z) {
                    double above = static_cast<double>(vertex.y) - before[i].y;
                    raw[v] = vertices[i];
                    raw[v].y = static_cast<float>(above + vertices[i].y);
                    found = true;
                }
            }
            if (!found)
                vertex.echo_out();
        }
        set_collision_vertices(sector, raw);
        memory::free_malloc(raw);
        memory::free_malloc(before);
        ParkLayout::singleton()->builder_->rebuild_collision();
    }
    memory::free_malloc(vertices);
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
    EditedRailPoint* next = following_;
    if (next->following_) {
        // Joined on to the previous segment unless the turn is sharper than RailEditorMaxJoinAngle.
        EditedRailPoint* next_next = next->following_;
        Vec4 before = {static_cast<float>(static_cast<double>(next->at_point_.x) - next_next->at_point_.x),
                       static_cast<float>(static_cast<double>(next->at_point_.y) - next_next->at_point_.y),
                       static_cast<float>(static_cast<double>(next->at_point_.z) - next_next->at_point_.z),
                       static_cast<float>(static_cast<double>(next->at_point_.w) - next_next->at_point_.w)};
        Vec4 here = {static_cast<float>(static_cast<double>(at_point_.x) - next->at_point_.x),
                     static_cast<float>(static_cast<double>(at_point_.y) - next->at_point_.y),
                     static_cast<float>(static_cast<double>(at_point_.z) - next->at_point_.z),
                     static_cast<float>(static_cast<double>(at_point_.w) - next->at_point_.w)};
        before.make_unit();
        here.make_unit();
        auto dot =
            static_cast<float>((static_cast<double>(here.z) * before.z + static_cast<double>(here.y) * before.y) +
                               static_cast<double>(here.x) * before.x);
        bool join = !(std::cos(static_cast<double>(script::fetch_float(kMaxJoinAngle)) * kAngleScaleToRad) > dot);
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
    uint8_t* geometry = fetch_shape(look_up_sector(kPostSource));
    int count = get_vertex_count(geometry);
    auto* vertices = static_cast<Vec4*>(memory::alloc_block(static_cast<uint32_t>(count) << 4));
    get_vertices(geometry, vertices);
    float low = kFar, high = -kFar;
    for (int i = 0; i < count; ++i) {
        float y = vertices[i].y;
        if (y < low)
            low = y;
        if (y > high)
            high = y;
    }
    // Leaning along the rail at an inner point: the post narrower as the rail climbs.
    float width = kPostWidth;
    if (following_ && preceding_) {
        double ax = static_cast<double>(preceding_->at_point_.x) - at_point_.x;
        double ay = static_cast<double>(preceding_->at_point_.y) - at_point_.y;
        auto az = static_cast<float>(static_cast<double>(preceding_->at_point_.z) - at_point_.z);
        auto aw = static_cast<float>(static_cast<double>(preceding_->at_point_.w) - at_point_.w);
        double bx = static_cast<double>(at_point_.x) - following_->at_point_.x;
        auto by = static_cast<float>(static_cast<double>(at_point_.y) - following_->at_point_.y);
        auto bz = static_cast<float>(static_cast<double>(at_point_.z) - following_->at_point_.z);
        auto bw = static_cast<float>(static_cast<double>(at_point_.w) - following_->at_point_.w);
        auto sx = static_cast<float>(bx + ax);
        double sy = static_cast<double>(by) + ay;
        double sz = static_cast<double>(bz) + az;
        auto sw = static_cast<float>(static_cast<double>(bw) + aw);
        Vec4 along = {static_cast<float>(static_cast<double>(sx) * 0.5f), static_cast<float>(sy * 0.5f),
                      static_cast<float>(sz * 0.5f), static_cast<float>(static_cast<double>(sw) * 0.5f)};
        along.make_unit();
        width = static_cast<float>(std::sqrt(1.0f - static_cast<double>(along.y) * along.y) * kPostWidth);
    }
    Vec4 at;
    transform_rail_point(&at, &at_point_, origin, position, angle);
    double drop = static_cast<double>(width) + high;
    for (int i = 0; i < count; ++i) {
        Vec4& v = vertices[i];
        v.x = static_cast<float>(static_cast<double>(at.x) + v.x);
        v.y = static_cast<float>(static_cast<double>(at.y) + v.y);
        v.z = static_cast<float>(static_cast<double>(at.z) + v.z);
        v.w = static_cast<float>(static_cast<double>(at.w) + v.w);
        v.y = static_cast<float>(static_cast<double>(v.y) - drop);
    }
    // The foot's vertices down to the ground.
    double foot = (static_cast<double>(level_y_) - drop) - high;
    for (int i : {8, 9, 10, 11, 1, 2, 5, 6})
        vertices[i].y = static_cast<float>(static_cast<double>(vertices[i].y) - foot);
    if (following_) {
        // Turned to face along the rail.
        double dx = static_cast<double>(at_point_.x) - following_->at_point_.x;
        double dz = static_cast<double>(at_point_.z) - following_->at_point_.z;
        double length = std::sqrt(dz * dz + dx * dx);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            dx *= inverse;
            dz *= inverse;
        }
        auto minus_z = static_cast<float>(-dz);
        for (int i = 0; i < count; ++i) {
            Vec4& v = vertices[i];
            double rx = static_cast<double>(v.x) - at.x;
            double rz = static_cast<double>(v.z) - at.z;
            v.x = static_cast<float>((dx * rx + rz * minus_z) + at.x);
            v.z = static_cast<float>((dz * rx + dx * rz) + at.z);
        }
    }
    if (!post_)
        post_ = duplicate_scene_sector(kPostSource);
    set_vertices(fetch_shape(post_), vertices);
    memory::free_malloc(vertices);
}

// 0x004F5D10
float EditedRailPoint::ground_height(const Vec4* position)
{
    render::ProbeRay feeler;
    Vec4 start = {position->x, static_cast<float>(static_cast<double>(position->y) + kGroundProbe), position->z,
                  position->w};
    feeler.change_origin(&start);
    Vec4 end = {position->x, static_cast<float>(static_cast<double>(position->y) - kGroundProbe), position->z,
                position->w};
    feeler.change_finish(&end);
    if (feeler.get_hit_shape(true, false)) {
        float height = feeler.contact_pos_.y;
        feeler.entity_.get_release();
        return height;
    }
    feeler.entity_.get_release();
    return 0.0f;
}

// 0x004F5E30
void EditedRailPoint::spotlight(bool flash, bool include_end_posts)
{
    if (highlighted_)
        return;
    uint32_t mask = flash ? flash_mask(script::fetch_int_value(kPulseRateKey)) : 0xFFFFFFFFu;
    Colour normal = {static_cast<uint8_t>(script::fetch_int_value(kHighlightR)),
                     static_cast<uint8_t>(script::fetch_int_value(kHighlightG)),
                     static_cast<uint8_t>(script::fetch_int_value(kHighlightB))};
    Colour bad = {static_cast<uint8_t>(script::fetch_int_value(kBadR)),
                  static_cast<uint8_t>(script::fetch_int_value(kBadG)),
                  static_cast<uint8_t>(script::fetch_int_value(kBadB))};
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
    auto paint = [mask](uint8_t* sector, const Colour& colour) {
        set_sector_colour(sector, colour.get_word());
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
    for (EditedRailPoint* p = point_chain_; p; p = p->following_) {
        double dx = static_cast<double>(p->at_point_.x) - origin->x;
        auto dy = static_cast<float>(static_cast<double>(p->at_point_.y) - origin->y);
        double dz = static_cast<double>(p->at_point_.z) - origin->z;
        float x;
        double z;
        turn_about_y(dx, dz, angle, &x, &z);
        p->at_point_.z = static_cast<float>(z + position->z);
        p->at_point_.w = static_cast<float>(0.0 + position->w); // (the original adds an unset local - here 0)
        p->at_point_.x = static_cast<float>(static_cast<double>(x) + position->x);
        p->at_point_.y = static_cast<float>(static_cast<double>(dy) + position->y);
    }
}

// 0x004F61C0
void EditedRail::draw_segments(const Vec4* origin, const Vec4* position, float angle)
{
    EditedRailPoint* last = nullptr;
    for (EditedRailPoint* p = point_chain_; p; p = p->following_)
        last = p;
    for (EditedRailPoint* p = last; p; p = p->preceding_)
        p->draw_segment(origin, position, angle);
}

// 0x004F6210
void EditedRail::draw_posts(const Vec4* origin, const Vec4* position, float angle)
{
    for (EditedRailPoint* p = point_chain_; p; p = p->following_)
        p->draw_post(origin, position, angle);
}

// 0x004F63B0
bool EditedRail::move_point(int index, const Vec4* position, bool /*updateSuperSectors*/)
{
    EditedRailPoint* p = fetch_vertex_at(index);
    bool valid = true;
    if (p->following_ && !rail_segment_valid(&p->following_->at_point_, position))
        valid = false;
    if (p->preceding_ && !rail_segment_valid(&p->preceding_->at_point_, position))
        valid = false;
    if (!ParkBuilderMode::is_inside_park(*position, 10.0f))
        valid = false;
    // Moved (even when not valid) unless a neighbour would be out of reach.
    if (p->following_ && distance_zyx(position, &p->following_->at_point_) > kMaxReach)
        return valid;
    if (p->preceding_ && distance_zyx(position, &p->preceding_->at_point_) > kMaxReach)
        return valid;
    p->at_point_ = *position;
    p->level_y_ =
        static_cast<float>(static_cast<double>(p->at_point_.y) - EditedRailPoint::ground_height(&p->at_point_));
    p->draw_post(&get_zero(), &get_zero(), 0.0f);
    p->draw_segment(&get_zero(), &get_zero(), 0.0f);
    if (p->preceding_)
        p->preceding_->draw_segment(&get_zero(), &get_zero(), 0.0f);
    return valid;
}

// --- RailAuthoring -----------------------------------------------------------------------------------------

// 0x004F6510
void RailAuthoring::restore_heights()
{
    change_live(false);
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following_) {
        for (EditedRailPoint* p = rail->point_chain_; p; p = p->following_) {
            if (p->has_post_)
                p->at_point_.y = static_cast<float>(static_cast<double>(EditedRailPoint::ground_height(&p->at_point_)) +
                                                    p->level_y_);
        }
    }
    change_live(true);
}

// 0x004F6560
void RailAuthoring::store_heights()
{
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following_) {
        for (EditedRailPoint* p = rail->point_chain_; p; p = p->following_)
            p->level_y_ =
                static_cast<float>(static_cast<double>(p->at_point_.y) - EditedRailPoint::ground_height(&p->at_point_));
    }
}

// 0x004F65A0
void RailAuthoring::draw_all_segments()
{
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following_)
        rail->draw_segments(&get_zero(), &get_zero(), 0.0f);
}

// 0x004F6600
void RailAuthoring::draw_all_posts()
{
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following_)
        rail->draw_posts(&get_zero(), &get_zero(), 0.0f);
}

// 0x004F6650
void RailAuthoring::reassemble()
{
    ParkLayout::singleton()->builder_->rebuild_collision();
    store_heights();
    draw_all_segments();
    draw_all_posts();
}

// 0x004F6AF0
void RailAuthoring::draw_dotted_line(const Vec4* position)
{
    if (!active_node_)
        return;
    EditedRailPoint* head = active_node_->point_chain_;
    if (!head)
        return;
    uint32_t source = kDottedGreen;
    if (rail_too_far(&head->at_point_, position)) {
        if (rail_out_of_reach(&head->at_point_, position)) {
            delete_dotted_line();
            return;
        }
        source = kDottedRed;
    } else if (head->following_ && !rail_corner_ok(&head->following_->at_point_, &head->at_point_, position)) {
        source = kDottedRed;
    }
    if (!dotted_line_ || source != dotted_line_name_) {
        delete_dotted_line();
        dotted_line_ = clone_rail_sector(source);
        dotted_line_name_ = source;
    }
    // (the original passes an unset local as the previous point: unread without a previous sector)
    stretch_rail_sector(dotted_line_name_, &head->at_point_, position, position, dotted_line_, nullptr, false);
    if (source == kDottedRed)
        set_sector_visibility(dotted_line_, flash_mask(script::fetch_int_value(kRedLineFlashRate)));
}

} // namespace park_editing
