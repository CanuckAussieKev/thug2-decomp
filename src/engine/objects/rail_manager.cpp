// The rail manager, 0x0057C170-0x0057EE65. See rail_manager.h.

#include "engine/objects/rail_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

#include "base/core/dbg.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "base/maths/mat4.h"
#include "base/maths/quat_rotation.h"
#include "base/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/smtptr.h"
#include "game/world/trick_objects.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/skate/skate_module.h"
#include "game/rider/walk_physics.h"
#include "game/park_editing/park_editor.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/core/checksums.h"

namespace objects {

namespace {
constexpr double kHalf = 0.5;
constexpr double kRailRaise = 6.0;
constexpr float kRailAlongLength = 7.0F;
constexpr double kRailDrop = 12.0;
constexpr double kRailLowerLimit = 2.0;            // (negated)
constexpr float kParallelDot = 0.99F;              // two rails this close to parallel (or anti-parallel)
constexpr double kRailJoinDistance = 6.0;
constexpr float kRailJoinTolerance = 6.0F;
constexpr float kFarthestStart = 1e14F;
constexpr double kDistanceSquaredScale = 4.0;
constexpr float kTwoThirds = 0.6666667F;
constexpr uint32_t kBigTableSlots = 0x4000;
constexpr uint32_t kSmallTableBytes = 0x8000;
constexpr int kEmptySlotByte = 0xFF;
constexpr float kNearestStart = 1e7F;
constexpr float kAlignmentBias = 1.122F;
constexpr double kAlignmentReach = 2.0;
constexpr uint32_t kOff = 0xD443A2BC; // Off

// Names.
constexpr uint32_t kClass = 0x12B4E660;                // Class
constexpr uint32_t kClimbingNode = 0x30C19600;         // ClimbingNode
constexpr uint32_t kType = 0x7321A8D6;                 // type
constexpr uint32_t kLadder = 0xC84243DA;               // Ladder
constexpr uint32_t kCreatedAtStart = 0x7C2552B9;       // CreatedAtStart
constexpr uint32_t kLipOverride = 0xF4C67F0F;          // LipOverride
constexpr uint32_t kStockRailFlag = 0x4DE721DE;        // DefaultLine
constexpr uint32_t kHangLeft = 0xF6DD21CA;             // HangLeft
constexpr uint32_t kHangRight = 0x5E9CE29B;            // HangRight
constexpr uint32_t kNoClimbing = 0xF22BFC6D;           // NoClimbing
constexpr uint32_t kSurfaceKindKey = 0x54CF8532;       // TerrainType
constexpr uint32_t kClimbMaxSnap = 0x30CE7F2C;         // Climb_Max_Snap (a script float)
constexpr uint32_t kRailMaxSnap = 0xF840753E;          // Rail_Max_Snap (a script float)
constexpr uint32_t kRailNode = 0x8E6B02AD;             // RailNode
constexpr uint32_t kTrickObject = 0x1645B830;          // TrickObject
constexpr uint32_t kAllRailsTrickObjects = 0xB675F8F6; // (a script integer: every rail a trick object)
constexpr uint32_t kName = 0xA1DC81F9;                 // name
constexpr uint32_t kCluster = 0x1A3A966B;              // Cluster
constexpr uint32_t kLinks = 0x2E7D5EE7;                // Links
constexpr uint32_t kLadderTrigger = 0xF5B17B09;        // TRG_Ladder03 (a node: counted 4 times as far)

// The matrix times `in` (all four components; 0x0040AF60, returns `out`).
maths::Vec4* xform(const maths::Mat4* matrix, maths::Vec4* out, const maths::Vec4* in)
{
    return &matrix->xform(*out, *in);
}

} // namespace

// 0x0057C170
// The spread of four values: the largest less the smallest.
double span4(const std::array<float, 4>& values)
{
    const float value_a = core::at(values, 0);
    const float value_b = core::at(values, 1);
    const float value_c = core::at(values, 2);
    const float value_d = core::at(values, 3);
    double low = value_a;
    if (value_b < value_a) {
        low = value_b;
    }
    low = std::min<double>(low, value_c);
    low = std::min<double>(low, value_d);
    double high = value_a;
    if (value_b > value_a) {
        high = value_b;
    }
    high = std::max<double>(high, value_c);
    if (value_d > high) {
        return value_d - low;
    }
    return high - low;
}

// 0x0057C200
bool GrindPoint::is_named(uint32_t name) const { return label_ == name; }

// 0x0057C260
void box_of(const maths::Vec4* a, const maths::Vec4* b, maths::Vec4* min, maths::Vec4* max)
{
    float min_x = 0.0F;
    float max_x = 0.0F;
    float min_y = 0.0F;
    float max_y = 0.0F;
    float min_z = 0.0F;
    float max_z = 0.0F;
    if (a->x < b->x) {
        min_x = a->x, max_x = b->x;
    } else {
        min_x = b->x, max_x = a->x;
    }
    if (a->y < b->y) {
        min_y = a->y, max_y = b->y;
    } else {
        min_y = b->y, max_y = a->y;
    }
    if (a->z < b->z) {
        min_z = a->z, max_z = b->z;
    } else {
        min_z = b->z, max_z = a->z;
    }
    min->x = min_x;
    min->y = min_y;
    min->w = 1.0F;
    min->z = min_z;
    max->y = max_y;
    max->x = max_x;
    max->w = 1.0F;
    max->z = max_z;
}

// 0x0057C330
void GrindRailHub::tidy_up()
{
    num_ = 0;
    memory::free_malloc(nodes_);
    nodes_ = nullptr;
    on_object_ = false;
    node_array_ = nullptr;
}

// 0x0057C360
double get_overlap(float a, float b, float value_c, float value_d)
{
    auto lengths =
        static_cast<float>(std::fabs(static_cast<double>(value_d) - value_c) + std::fabs(static_cast<double>(b) - a));
    return lengths - span4({{a, b, value_c, value_d}});
}

// 0x0057C3A0
int nearly_equal4(float a, float b, float value_c, float value_d, float tolerance)
{
    if (std::fabs(static_cast<double>(a) - b) < tolerance && std::fabs(static_cast<double>(b) - value_c) < tolerance &&
        std::fabs(static_cast<double>(value_c) - value_d) < tolerance) {
        return 1;
    }
    return 0;
}

// 0x0057C3F0
void GrindRailHub::move_node(uint32_t name, const maths::Vec4* pos) const
{
    const std::span<GrindPoint> run = node_run();
    std::size_t index = 0;
    while (index < run.size() && core::at(run, index).label() != name) {
        ++index;
    }
    if (index >= run.size()) {
        return;
    }
    GrindPoint* node = &core::at(run, index);
    node->at_point() = *pos;
    if (const GrindPoint* next = node->succ()) {
        const maths::Vec4 here = node->at_point();
        const maths::Vec4 there = next->at_point();
        box_of(&here, &there, &node->lower_limit(), &node->upper_limit());
    }
    if (node->last_value()) {
        // (the original tests the found node's own link each time, not each node's)
        for (int i = 0; i < num_; ++i) {
            GrindPoint* each = &core::at(node_run(), index);
            if (each->succ() == node) {
                box_of(&each->at_point(), &node->at_point(), &each->lower_limit(), &each->upper_limit());
            }
        }
    }
}

// 0x0057C4F0
maths::Mat4& GrindRailHub::change_body_transform(const maths::Mat4& matrix)
{
    on_object_ = true;
    return frame_xform_ = matrix;
}

// 0x0057C500
LadderHit* get_assign(LadderHit& hit, const LadderHit* other)
{
    hit = *other;
    return &hit;
}

// 0x0057C540
RailHit* get_assign(RailHit& hit, const RailHit* other)
{
    hit = *other;
    return &hit;
}

// 0x0057C5A0
bool nodes_together(const GrindPoint* a, const GrindPoint* b)
{
    // (NaN counts as near: the original only fails on "greater")
    return !(std::fabs(static_cast<double>(a->at_point().x) - b->at_point().x) > 1.0) &&
           !(std::fabs(static_cast<double>(a->at_point().y) - b->at_point().y) > 1.0) &&
           !(std::fabs(static_cast<double>(a->at_point().z) - b->at_point().z) > 1.0);
}

namespace {
RailClearOverride& rail_clear_override()
{
    static core::NoDestroy<RailClearOverride> rail_clear_override; // (NOT ORIGINAL: set_rail_clear_override)
    return rail_clear_override.get();
}
} // namespace

void set_rail_clear_override(RailClearOverride override) { rail_clear_override() = override; }

// 0x0057C600
bool rail_clear(maths::Vec4 a, maths::Vec4 b)
{
    if (park_editing::ParkBuilderMode::sole()->editor_state() == kOff) { // (the park editor's)
        return true;
    }
    if (rail_clear_override() &&
        rail_clear_override()(&a, &b)) { // (NOT ORIGINAL: the level editor's flat-ground rails)
        return true;
    }
    render::ProbeRay feeler; // (what it hit let go of by its destructor, where the original lets go inline)
    // A quarter of the way from a to b, 6 above (the original's rounding: some sums kept unrounded).
    const double sum_w = static_cast<double>(b.w) + a.w;
    const double sum_z = static_cast<double>(b.z) + a.z;
    auto sum_y = static_cast<float>(static_cast<double>(b.y) + a.y);
    auto sum_x = static_cast<float>(static_cast<double>(b.x) + a.x);
    auto half_w = static_cast<float>(sum_w * kHalf);
    const double half_z = sum_z * 0.5;
    const double half_y = sum_y * 0.5;
    auto half_x = static_cast<float>(sum_x * kHalf);
    auto to_w = static_cast<float>(static_cast<double>(half_w) - a.w);
    auto to_z = static_cast<float>(half_z - a.z);
    const double to_y = half_y - a.y;
    const double to_x = static_cast<double>(half_x) - a.x;
    auto quarter_w = static_cast<float>(to_w * kHalf);
    auto quarter_z = static_cast<float>(to_z * kHalf);
    auto quarter_y = static_cast<float>(to_y * kHalf);
    const double quarter_x = to_x * 0.5;
    auto at_w = static_cast<float>(static_cast<double>(a.w) + quarter_w);
    auto at_z = static_cast<float>(static_cast<double>(a.z) + quarter_z);
    const double at_y = static_cast<double>(a.y) + quarter_y;
    auto at_x = static_cast<float>(static_cast<double>(a.x) + quarter_x);
    auto high_y = static_cast<float>(at_y + kRailRaise);
    // Across the rail (horizontally), 7 long (0x0042BBF0: the length set).
    maths::Vec4 along = {.x = static_cast<float>(static_cast<double>(b.x) - a.x),
                         .y = 0.0F,
                         .z = static_cast<float>(static_cast<double>(b.z) - a.z),
                         .w = static_cast<float>(static_cast<double>(b.w) - a.w)};
    along.set_length(kRailAlongLength);
    const float across_x = along.z;
    const float across_y = along.y;
    const auto across_z = static_cast<float>(-static_cast<double>(along.x));
    const float across_w = along.w;
    auto sub = [](float lhs, float rhs) { return static_cast<float>(static_cast<double>(lhs) - rhs); };
    auto add = [](float lhs, float rhs) { return static_cast<float>(static_cast<double>(lhs) + rhs); };
    const maths::Vec4 minus = {.x = sub(at_x, across_x), .y = sub(high_y, across_y), .z = sub(at_z, across_z), .w = sub(at_w, across_w)};
    const maths::Vec4 plus = {.x = add(across_x, at_x), .y = add(across_y, high_y), .z = add(across_z, at_z), .w = add(across_w, at_w)};
    // Anything across it (either way): not clear.
    feeler.change_strand(&plus, &minus);
    if (feeler.get_hit_shape(true, false) != 0) {
        feeler.entity().get_release();
        return false;
    }
    std::swap(feeler.line().start, feeler.line().end); // (0x0057C210: the two halves swapped)
    if (feeler.get_hit_shape(true, false) != 0) {
        feeler.entity().get_release();
        return false;
    }
    // Down 12 from one side: nothing there, clear.
    maths::Vec4 top = plus;
    maths::Vec4 bottom = {.x = plus.x, .y = static_cast<float>(plus.y - kRailDrop), .z = plus.z, .w = static_cast<float>(plus.w + 1.0)};
    feeler.change_strand(&top, &bottom);
    if (feeler.get_hit_shape(true, false) == 0) {
        feeler.entity().get_release();
        return true;
    }
    auto base = static_cast<float>(high_y - kRailRaise);
    auto height_one = static_cast<float>(static_cast<double>(feeler.contact_pos().y) - base);
    // And from the other.
    top = minus;
    bottom = {.x = minus.x, .y = static_cast<float>(minus.y - kRailDrop), .z = minus.z, .w = static_cast<float>(minus.w + 1.0)};
    feeler.change_strand(&top, &bottom);
    if (feeler.get_hit_shape(true, false) == 0) {
        return true;
    }
    const double height_two = static_cast<double>(feeler.contact_pos().y) - base;
    // Ground on both sides: clear only if one side is at the rail's height (within 1) or 2 below it.
    if (height_one > -1.0F && height_two > -1.0) { // (NaN: on to the next test)
        return false;
    }
    double higher = 0.0;
    double lower = 0.0;
    if (height_one < height_two) {
        higher = static_cast<float>(height_two);
        lower = height_one;
    } else {
        higher = height_one;
        lower = height_two;
    }
    if (!(higher > 1.0)) {
        return true;
    }
    return !(lower > -kRailLowerLimit);
}

// 0x0057CB20
maths::Vec4* GrindRailHub::to_world(maths::Vec4* out, const maths::Vec4* in)
{
    if (!on_object_) {
        *out = *in;
        return out;
    }
    xform(&frame_xform_, out, in);
    return out;
}

// 0x0057CB60
maths::Vec4* GrindRailHub::get_absolute_pos(maths::Vec4* out, const GrindPoint* node)
{
    if (!on_object_) {
        *out = node->at_point();
        return out;
    }
    maths::Vec4 world;
    xform(&frame_xform_, &world, &node->at_point());
    *out = world;
    return out;
}

// 0x0057CBD0
GrindPoint* GrindRailHub::find_node(uint32_t name)
{
    if (!nodes_) {
        return nullptr;
    }
    for (int i = 0; i < num_; ++i) {
        if (core::at(node_run(), i).label() == name) {
            return &core::at(node_run(), i);
        }
    }
    return nullptr;
}

// 0x0057CC30
int GrindPoint::get_wall_side(const maths::Vec4* direction)
{
    const maths::Vec4 this_point = at_point_;
    const maths::Vec4 next_point = succ_->at_point_;
    render::ProbeRay feeler; // (what it hit let go of by its destructor, where the original lets go inline)
    // A quarter of the way along the segment, 6 below it (the original's rounding: some sums kept unrounded).
    auto sum_y = static_cast<float>(static_cast<double>(next_point.y) + this_point.y);
    auto sum_x = static_cast<float>(static_cast<double>(next_point.x) + this_point.x);
    auto half_w = static_cast<float>((static_cast<double>(next_point.w) + this_point.w) * kHalf);
    const double half_z = (static_cast<double>(next_point.z) + this_point.z) * 0.5;
    const double half_y = sum_y * 0.5;
    auto half_x = static_cast<float>(sum_x * kHalf);
    auto to_w = static_cast<float>(half_w - static_cast<double>(this_point.w));
    auto to_z = static_cast<float>(half_z - this_point.z);
    const double to_y = half_y - this_point.y;
    const double to_x = static_cast<double>(half_x) - this_point.x;
    auto quarter_w = static_cast<float>(to_w * kHalf);
    auto quarter_z = static_cast<float>(to_z * kHalf);
    auto quarter_y = static_cast<float>(to_y * kHalf);
    const double quarter_x = to_x * 0.5;
    auto at_w = static_cast<float>(static_cast<double>(this_point.w) + quarter_w);
    auto at_z = static_cast<float>(static_cast<double>(this_point.z) + quarter_z);
    const double at_y = static_cast<double>(this_point.y) + quarter_y;
    auto at_x = static_cast<float>(quarter_x + this_point.x);
    auto low_y = static_cast<float>(at_y - kRailRaise);
    // Across the segment (horizontally), 4 long.
    const double dw = static_cast<double>(next_point.w) - this_point.w;
    auto dz = static_cast<float>(static_cast<double>(next_point.z) - this_point.z);
    const double dx = static_cast<double>(next_point.x) - this_point.x;
    auto dx_stored = static_cast<float>(dx);
    float across_y = 0.0F;
    float across_x = dz;
    const double length = std::sqrt((dx * dx) + (static_cast<double>(dz) * dz));
    double along_x = dx;
    if (length > 0.0) {
        const double scale = 4.0 / length;
        along_x = dx * scale;
        across_y = static_cast<float>(0.0 * scale);
        across_x = static_cast<float>(dz * scale);
    }
    auto across_z = static_cast<float>(-along_x);
    const maths::Vec4 from = {.x = static_cast<float>(static_cast<double>(across_x) + at_x),
                              .y = static_cast<float>(static_cast<double>(across_y) + low_y),
                              .z = static_cast<float>(static_cast<double>(across_z) + at_z), .w = static_cast<float>(dw + at_w)};
    const maths::Vec4 to = {.x = static_cast<float>(static_cast<double>(at_x) - across_x),
                            .y = static_cast<float>(static_cast<double>(low_y) - across_y),
                            .z = static_cast<float>(static_cast<double>(at_z) - across_z),
                            .w = static_cast<float>(static_cast<double>(at_w) - dw)};
    feeler.change_strand(&from, &to);
    const bool hit_one_way = feeler.get_hit_shape(true, false) != 0;
    const maths::Vec4 start = feeler.line().start;
    feeler.line().start = feeler.line().end;
    feeler.line().end = start;
    const bool hit_other_way = feeler.get_hit_shape(true, false) != 0;
    int side = 0;
    if (hit_one_way != hit_other_way) {
        side = hit_one_way ? -1 : 1;
        const double dot = (static_cast<double>(dx_stored) * direction->x) +
                           (((static_cast<double>(next_point.y) - this_point.y) * direction->y) +
                            (static_cast<double>(dz) * direction->z));
        if (dot < 0.0) {
            side = -side;
        }
    }
    feeler.entity().get_release();
    return side;
}

// 0x0057CFB0
maths::Mat4* GrindRailHub::get_climb_matrix(maths::Mat4* out, const GrindPoint* node)
{
    maths::Mat4 local;
    maths::quat_to_matrix(node->climb(), &local); // the quaternion's
    core::at(local.row, 0).w = 0.0F;
    core::at(local.row, 1).w = 0.0F;
    core::at(local.row, 2).w = 0.0F;
    core::at(local.row, 3) = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    if (!on_object_) {
        *out = local;
    } else {
        maths::Mat4 world;
        maths::get_multiply(world, frame_xform_, local);
        *out = world;
    }
    return out;
}

// 0x0057D090
void GrindRailHub::append_vertex(uint32_t name, script::Struct* params) const
{
    GrindPoint* node = &core::at(node_run(), adding_);
    uint32_t node_class = 0; // (the original's are uninitialised: read only if the node has them)
    params->read_hash_value(kClass, &node_class, 0);
    const bool climbing = node_class == kClimbingNode;
    uint32_t type = 0;
    params->read_hash_value(kType, &type, 0);
    node->set_succ(nullptr);
    node->last_value() = nullptr;
    node->set_label(name);
    node->flag_bits() = 0;
    if (params->has_bare_flag(kCreatedAtStart)) {
        node->flag_bits() |= kRailFlagActive;
    } else {
        node->flag_bits() &= ~kRailFlagActive;
    }
    level_nodes::fetch_place(params, &node->at_point());
    if (params->has_bare_flag(kLipOverride)) {
        node->flag_bits() |= kRailFlagLipOverride;
    }
    if (params->has_bare_flag(kStockRailFlag)) {
        node->flag_bits() |= kRailFlagDefaultLine;
    }
    if (climbing) {
        node->flag_bits() |= kRailFlagClimbing;
        if (type == kLadder) {
            node->flag_bits() |= kRailFlagLadder;
        } else {
            node->flag_bits() &= ~kRailFlagLadder;
        }
    }
    auto flag = [&](uint32_t name, uint32_t bit) {
        if (params->has_bare_flag(name)) {
            node->flag_bits() |= bit;
        } else {
            node->flag_bits() &= ~bit;
        }
    };
    flag(kHangLeft, kRailFlagHangLeft);
    flag(kHangRight, kRailFlagHangRight);
    flag(kNoClimbing, kRailFlagNoClimbing);
    if (climbing) {
        // Its orientation from its angles, as a quaternion.
        maths::Vec4 angles;
        level_nodes::read_turn_angles(params, &angles);
        maths::Mat4 matrix;
        const maths::Mat4* built = &matrix.init_from_angles(angles);
        node->set_climb(maths::quat_from_matrix(*built));
    } else {
        node->set_surface_type(0);
        uint32_t terrain = 0; // (the original's: its params argument's slot, the low byte the climbing flag)
        params->read_hash_value(kSurfaceKindKey, &terrain, 1);
        node->set_surface_type(surface_kind::get_terrain_index(terrain));
    }
    box_of(&node->at_point(), &node->at_point(), &node->lower_limit(), &node->upper_limit());
}

// 0x0057D260
void GrindRailHub::remove_overlaps() const
{
    debug::printf("Starting overlapping rail removal\n");
    int removed = 0;
    for (int i = 0; i < num_; ++i) {
        GrindPoint* grind_a = &core::at(node_run(), i);
        const GrindPoint* a_next = grind_a->succ();
        if (!a_next) {
            continue;
        }
        const maths::Vec4 a_start = grind_a->at_point();
        const maths::Vec4 a_end = a_next->at_point();
        // Its direction (each step stored as a float, as the original), made unit length.
        auto dx = static_cast<float>(static_cast<double>(a_end.x) - a_start.x);
        auto dy = static_cast<float>(static_cast<double>(a_end.y) - a_start.y);
        auto dz = static_cast<float>(static_cast<double>(a_end.z) - a_start.z);
        const float raw_x = dx;
        const float raw_y = dy;
        const float raw_z = dz;
        const double length =
            std::sqrt((static_cast<double>(dy) * dy) + (static_cast<double>(dx) * dx) + (static_cast<double>(dz) * dz));
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            dx = static_cast<float>(dx * inverse);
            dy = static_cast<float>(dy * inverse);
            dz = static_cast<float>(dz * inverse);
        }
        // Its box, 16 bigger each way.
        const auto lo_x = static_cast<float>(grind_a->lower_limit().x - 16.0);
        const auto lo_y = static_cast<float>(grind_a->lower_limit().y - 16.0);
        const auto lo_z = static_cast<float>(grind_a->lower_limit().z - 16.0);
        const auto hi_x = static_cast<float>(grind_a->upper_limit().x + 16.0);
        const auto hi_y = static_cast<float>(grind_a->upper_limit().y + 16.0);
        const auto hi_z = static_cast<float>(grind_a->upper_limit().z + 16.0);
        for (int j = i + 1; j < num_; ++j) {
            if ((grind_a->flag_bits() & kRailFlagActive) == 0U) {
                break;
            }
            GrindPoint* grind_b = &core::at(node_run(), j);
            const GrindPoint* b_next = grind_b->succ();
            if (!b_next) {
                continue;
            }
            // (the tests as the original's: NaN passes them)
            if (grind_b->lower_limit().x > hi_x || grind_b->upper_limit().x < lo_x || grind_b->lower_limit().z > hi_z ||
                grind_b->upper_limit().z < lo_z || grind_b->lower_limit().y > hi_y || grind_b->upper_limit().y < lo_y) {
                continue;
            }
            const maths::Vec4 b_start = grind_b->at_point();
            const maths::Vec4 b_end = b_next->at_point();
            maths::Vec4 along = {.x = static_cast<float>(static_cast<double>(b_end.x) - b_start.x),
                                 .y = static_cast<float>(static_cast<double>(b_end.y) - b_start.y),
                                 .z = static_cast<float>(static_cast<double>(b_end.z) - b_start.z),
                                 .w = static_cast<float>(static_cast<double>(b_end.w) - b_start.w)};
            along.make_unit();
            const double dot = (static_cast<double>(along.y) * dy) + (static_cast<double>(along.x) * dx) +
                               (static_cast<double>(along.z) * dz);
            // Parallel (or opposite) to within about 8 degrees.
            if (!(dot < -kParallelDot) && !(dot > kParallelDot)) {
                continue;
            }
            const maths::Vec4 offset = {.x = static_cast<float>(static_cast<double>(b_start.x) - a_start.x),
                                        .y = static_cast<float>(static_cast<double>(b_start.y) - a_start.y),
                                        .z = static_cast<float>(static_cast<double>(b_start.z) - a_start.z),
                                        .w = static_cast<float>(static_cast<double>(b_start.w) - a_start.w)};
            maths::Vec4 direction = {
                .x = raw_x, .y = raw_y, .z = raw_z, .w = static_cast<float>(static_cast<double>(a_end.w) - a_start.w)};
            direction.make_unit();
            maths::Vec4 apart = offset;
            apart.flatten_onto_plane(direction);
            const double distance =
                std::sqrt((static_cast<double>(apart.x) * apart.x) + (static_cast<double>(apart.z) * apart.z) +
                          (static_cast<double>(apart.y) * apart.y));
            if (!(distance < kRailJoinDistance)) {
                continue;
            }
            int matches = get_overlap(a_start.x, a_end.x, b_start.x, b_end.x) > kRailJoinDistance ? 1 : 0;
            if (get_overlap(a_start.z, a_end.z, b_start.z, b_end.z) > kRailJoinDistance) {
                ++matches;
            }
            int same = (nearly_equal4(a_start.x, a_end.x, b_start.x, b_end.x, kRailJoinTolerance) != 0) ? 1 : 0;
            if (nearly_equal4(a_start.z, a_end.z, b_start.z, b_end.z, kRailJoinTolerance) != 0) {
                ++same;
            }
            if (same + matches < 2) {
                continue;
            }
            // Overlapping: the lower one goes.
            ++removed;
            if (grind_a->at_point().y < grind_b->at_point().y) {
                grind_a->flag_bits() &= ~kRailFlagActive;
            } else {
                grind_b->flag_bits() &= ~kRailFlagActive;
            }
            break;
        }
    }
    debug::printf("Done overlapping rail removal, removed %d\n", removed);
}

// 0x0057D7A0
void GrindRailHub::remove_overlaps_if_not_park() const
{
    if (park_editing::ParkBuilderMode::sole()->editor_state() == kOff) { // (the park editor's level)
        return;
    }
    remove_overlaps();
}

// 0x0057D7C0
void GrindRailHub::change_live(checksums::Name rail, int active, bool entire_rail) const
{
    const uint32_t name = rail.value();
    const std::span<GrindPoint> run = node_run();
    std::size_t index = 0;
    while (index < run.size() && core::at(run, index).label() != name) {
        ++index;
    }
    if (index >= run.size()) {
        return;
    }
    GrindPoint* node = &core::at(run, index);
    if (entire_rail) {
        // This node and every one after it.
        for (; node; node = node->succ()) {
            if (active != 0) {
                node->flag_bits() |= kRailFlagActive;
            } else {
                node->flag_bits() &= ~kRailFlagActive;
            }
        }
        return;
    }
    if (active != 0) {
        node->flag_bits() |= kRailFlagActive;
    } else {
        node->flag_bits() &= ~kRailFlagActive;
    }
}

// 0x0057D840
bool GrindRailHub::is_operative(uint32_t name) const
{
    for (int index = 0; index < num_; ++index) {
        if (core::at(node_run(), index).label() == name) {
            return (core::at(node_run(), index).flag_bits() & kRailFlagActive) != 0;
        }
    }
    return false;
}

// 0x0057D880
bool GrindRailHub::find_ladder(const maths::Vec4* pos, float radius, float height, bool up, void* checker,
                               LadderHit* result, GrindPoint** found)
{
    auto radius_squared = static_cast<float>(static_cast<double>(radius) * radius);
    for (int index = num_; index != 0;) {
        GrindPoint* node = &core::at(node_run(), --index);
        if (((node->flag_bits() & kRailFlagLadder) == 0U) || ((node->flag_bits() & kRailFlagActive) == 0U)) {
            continue;
        }
        if (std::fabs(static_cast<double>(node->at_point().x) - pos->x) > radius ||
            std::fabs(static_cast<double>(node->at_point().z) - pos->z) > radius ||
            std::fabs(static_cast<double>(node->at_point().y) - pos->y) > height) {
            continue;
        }
        const double dx = static_cast<double>(node->at_point().x) - pos->x;
        const double dz = static_cast<double>(node->at_point().z) - pos->z;
        if ((dz * dz) + (dx * dx) > radius_squared) {
            continue;
        }
        GrindPoint* other = node->succ() ? node->succ() : node->last_value();
        maths::Mat4 matrix;
        if (up) {
            const maths::Mat4* orientation = get_climb_matrix(&matrix, node);
            if (static_cast<FootMovement*>(checker)->check_ladder_bottom(
                    &node->at_point(), &other->at_point(), orientation, result)) {
                *found = node;
                return true;
            }
        } else {
            const maths::Mat4* orientation = get_climb_matrix(&matrix, other);
            if (objects::FootMovement::check_ladder_top(&other->at_point(), &node->at_point(), orientation, result)) {
                *found = other;
                return true;
            }
        }
    }
    return false;
}

// 0x0057DA20
bool GrindRailHub::find_ladder_on_line(const maths::Vec4* start, const maths::Vec4* end, void* checker,
                                       LadderHit* result, GrindPoint** found)
{
    *found = nullptr;
    maths::Seg line{};
    line.start = *start;
    line.end = *end;
    maths::Vec4 min;
    maths::Vec4 max;
    box_of(&line.start, &line.end, &min, &max);
    const double snap = script::fetch_float(kClimbMaxSnap);
    min.x = static_cast<float>(min.x - snap);
    float best = kFarthestStart;
    min.y = static_cast<float>(min.y - snap);
    min.z = static_cast<float>(min.z - snap);
    max.x = static_cast<float>(max.x + snap);
    max.y = static_cast<float>(max.y + snap);
    max.z = static_cast<float>(max.z + snap);
    auto snap_squared = static_cast<float>(snap * snap);
    for (int index = 0; index < num_; ++index) {
        GrindPoint* node = &core::at(node_run(), index);
        if (((node->flag_bits() & kRailFlagLadder) == 0U) || ((node->flag_bits() & kRailFlagActive) == 0U) ||
            !node->succ()) {
            continue;
        }
        // (the tests as the original's: NaN passes them)
        if (min.x > node->upper_limit().x || max.x < node->lower_limit().x || min.z > node->upper_limit().z ||
            max.z < node->lower_limit().z || min.y > node->upper_limit().y || max.y < node->lower_limit().y) {
            continue;
        }
        GrindPoint* next = node->succ();
        maths::Seg rail{};
        rail.start = node->at_point();
        rail.end = next->at_point();
        maths::Vec4 on_line;
        maths::Vec4 on_rail;
        float along_line = 0.0F;
        float along_rail = 0.0F;
        if (!maths::closest_points_between_lines(line, rail, on_line, on_rail, along_line, along_rail, true)) {
            continue;
        }
        const maths::Vec4 apart = on_rail - on_line;
        float distance_squared = apart.magnitude_squared();
        if (node->label() == kLadderTrigger || next->label() == kLadderTrigger) {
            distance_squared = static_cast<float>(distance_squared * kDistanceSquaredScale);
        }
        if (distance_squared > snap_squared || distance_squared > best) {
            continue;
        }
        LadderHit record;
        maths::Mat4 matrix;
        if (node->at_point().y < next->at_point().y) {
            // Climbing up from this node.
            const maths::Mat4* orientation = get_climb_matrix(&matrix, node);
            if (!static_cast<FootMovement*>(checker)->check_ladder_line(&node->at_point(), &next->at_point(),
                                                                        orientation, &on_rail, along_rail, &record)) {
                continue;
            }
            *found = node;
        } else {
            const maths::Mat4* orientation = get_climb_matrix(&matrix, next);
            if (!static_cast<FootMovement*>(checker)->check_ladder_line(
                    &next->at_point(), &node->at_point(), orientation, &on_rail, static_cast<float>(1.0 - along_rail),
                    &record)) {
                continue;
            }
            *found = next;
        }
        best = distance_squared;
        get_assign(*result, &record);
    }
    return *found != nullptr;
}

// 0x0057DDA0
bool GrindRailHub::find_rail(const maths::Vec4* start, const maths::Vec4* end, const maths::Vec4* direction,
                             void* checker, RailHit* result, float margin)
{
    bool found = false;
    maths::Seg line{};
    line.start = *start;
    line.end = *end;
    maths::Vec4 min;
    maths::Vec4 max;
    box_of(&line.start, &line.end, &min, &max);
    min.x = static_cast<float>(static_cast<double>(min.x) - margin);
    float best = kFarthestStart;
    min.y = static_cast<float>(static_cast<double>(min.y) - margin);
    min.z = static_cast<float>(static_cast<double>(min.z) - margin);
    max.x = static_cast<float>(static_cast<double>(max.x) + margin);
    max.y = static_cast<float>(static_cast<double>(max.y) + margin);
    max.z = static_cast<float>(static_cast<double>(max.z) + margin);
    auto margin_squared = static_cast<float>(static_cast<double>(margin) * margin);
    for (int index = 0; index < num_; ++index) {
        GrindPoint* node = &core::at(node_run(), index);
        if (((node->flag_bits() & kRailFlagLadder) != 0U) || ((node->flag_bits() & kRailFlagNoClimbing) != 0U) ||
            ((node->flag_bits() & kRailFlagActive) == 0U) || !node->succ()) {
            continue;
        }
        // (the tests as the original's: NaN passes them)
        if (min.x > node->upper_limit().x || max.x < node->lower_limit().x || min.z > node->upper_limit().z ||
            max.z < node->lower_limit().z || min.y > node->upper_limit().y || max.y < node->lower_limit().y) {
            continue;
        }
        const GrindPoint* next = node->succ();
        maths::Seg rail{};
        rail.start = node->at_point();
        rail.end = next->at_point();
        maths::Vec4 on_line;
        maths::Vec4 on_rail;
        float along_line = 0.0F;
        float along_rail = 0.0F;
        if (!maths::closest_points_between_lines(line, rail, on_line, on_rail, along_line, along_rail, true)) {
            continue;
        }
        maths::Vec4 apart = on_rail - on_line;
        if (apart.magnitude_squared() > margin_squared) {
            continue;
        }
        // Distance along `direction` counts a third.
        const double along = (static_cast<double>(apart.z) * direction->z) +
                             (static_cast<double>(apart.y) * direction->y) +
                             (static_cast<double>(apart.x) * direction->x);
        if (along > 0.0) {
            apart -= static_cast<float>(along * kTwoThirds) * *direction;
        }
        const float distance_squared = apart.magnitude_squared();
        if (distance_squared > best) {
            continue;
        }
        maths::Vec4 node_world;
        maths::Vec4 next_world;
        get_absolute_pos(&next_world, next);
        get_absolute_pos(&node_world, node);
        if (!rail_clear(node_world, next_world)) {
            continue;
        }
        RailHit record{};
        record.node = node;
        maths::Vec4 line_world;
        maths::Vec4 rail_world;
        maths::Vec4 next_at;
        maths::Vec4 node_at;
        const maths::Vec4* line_at = to_world(&line_world, &on_line);
        const maths::Vec4* rail_at = to_world(&rail_world, &on_rail);
        const maths::Vec4* next_pos = get_absolute_pos(&next_at, next);
        const maths::Vec4* node_pos = get_absolute_pos(&node_at, node);
        if (!static_cast<FootMovement*>(checker)->check_rail(node_pos, next_pos, rail_at, line_at, along_rail, &record,
                                                             false)) {
            continue;
        }
        best = distance_squared;
        get_assign(*result, &record);
        found = true;
    }
    return found;
}

// 0x0057E170
bool GrindRailHub::find_joining_rail(const GrindPoint* from, uint32_t exclude_flags, GrindPoint** found)
{
    for (int index = 0; index < num_; ++index) {
        GrindPoint* node = &core::at(node_run(), index);
        // (the tests as the original's: NaN passes them)
        if (std::fabs(static_cast<double>(node->at_point().x) - from->at_point().x) > 1.0 ||
            std::fabs(static_cast<double>(node->at_point().y) - from->at_point().y) > 1.0 ||
            std::fabs(static_cast<double>(node->at_point().z) - from->at_point().z) > 1.0) {
            continue;
        }
        const GrindPoint* next = node->succ();
        if (!next && !node->last_value()) {
            continue;
        }
        if (node == from) {
            continue;
        }
        if (next && ((node->flag_bits() & kRailFlagActive) != 0U) && ((exclude_flags & node->flag_bits()) == 0U)) {
            maths::Vec4 node_world;
            maths::Vec4 next_world;
            get_absolute_pos(&next_world, next);
            get_absolute_pos(&node_world, node);
            if (rail_clear(node_world, next_world)) {
                *found = node;
                return true;
            }
        }
        const GrindPoint* prev = node->last_value();
        if (prev && ((prev->flag_bits() & kRailFlagActive) != 0U) && ((exclude_flags & prev->flag_bits()) == 0U)) {
            maths::Vec4 prev_world;
            maths::Vec4 node_world;
            get_absolute_pos(&node_world, node);
            get_absolute_pos(&prev_world, node->last_value());
            if (rail_clear(prev_world, node_world)) {
                *found = node->last_value();
                return true;
            }
        }
    }
    return false;
}

// 0x0057E960
void GrindRailHub::add_from_node_array(script::Array* nodes)
{
    node_array_ = nodes;
    auto skipped = [](script::Struct* node) { // (the skate module's 0x00565BB0: a node this game leaves out)
        return modules::Skate::should_be_absent(node) != 0;
    };
    auto is_rail = [](script::Struct* node) {
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        return node_class == kRailNode || node_class == kClimbingNode;
    };
    num_ = 0;
    for (uint32_t i = 0; i < nodes->extent(); ++i) {
        auto* node = nodes->struct_at(i);
        if (!skipped(node) && is_rail(node)) {
            ++num_;
        }
    }
    if (num_ == 0) {
        return;
    }
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->bottom_up_heap());
    nodes_ = static_cast<GrindPoint*>(memory::alloc_block(num_ * sizeof(GrindPoint)));
    // A hash table of the nodes' names (0x4000 slots, linear probing) to their indexes.
    // NOT ORIGINAL: with 0x2000 rail nodes or more (THP8's whole town has ~18,000; past 0x4000 the original's table
    // was full and adding went round it for ever) a bigger one, 32-bit indexes, at most a quarter full. Original
    // behaviour: 0x4000 16-bit slots whatever the count.
    const bool big = num_ >= 0x2000;
    uint32_t slots = kBigTableSlots;
    while (big && slots < static_cast<uint32_t>(num_) * 4) {
        slots <<= 1;
    }
    const uint32_t mask = slots - 1;
    void* table_memory = memory::alloc_block(big ? slots * 4 : kSmallTableBytes);
    memory::Hub::get_pull()->leave_heap_scope();
    std::memset(table_memory, kEmptySlotByte, big ? slots * 4 : kSmallTableBytes);
    const std::span<int16_t> small(static_cast<int16_t*>(table_memory), big ? 0 : 0x4000);
    const std::span<int32_t> wide(static_cast<int32_t*>(table_memory), big ? slots : 0);
    auto entry = [&](uint32_t slot) { return big ? core::at(wide, slot) : core::at(small, slot); };
    adding_ = 0;
    const bool all_trick_objects = script::fetch_int_value(kAllRailsTrickObjects) != 0;
    for (uint32_t i = 0; i < nodes->extent(); ++i) {
        auto* node = nodes->struct_at(i);
        if (skipped(node) || !is_rail(node)) {
            continue;
        }
        const uint32_t name = level_nodes::get_node_name(node);
        append_vertex(name, node);
        uint32_t slot = name & mask;
        while (entry(slot) != -1) {
            slot = (slot + 1) & mask;
        }
        if (big) {
            core::at(wide, slot) = adding_;
        } else {
            core::at(small, slot) = static_cast<int16_t>(adding_);
        }
        const bool trick_object = node->has_entry_named(kTrickObject);
        if (all_trick_objects || trick_object) {
            // A trick object of its cluster (the park / trick object manager 0x007CE500).
            uint32_t node_name = 0;
            node->read_hash_value(kName, &node_name, 1);
            uint32_t cluster = node_name;
            node->read_hash_value(kCluster, &cluster, 1);
            auto* trick_objects = trick_spot_hub();
            trick_objects->add_cluster(cluster);
            trick_objects->add_object_to_cluster(node_name, cluster);
        }
        ++adding_;
    }
    // The links: each node's Links (by name, through the table).
    for (int i = 0; i < num_; ++i) {
        GrindPoint* node = &core::at(node_run(), i);
        script::Struct* params = level_nodes::fetch_vertex(node->label(), true);
        script::Array* links = nullptr;
        params->fetch_list(kLinks, &links, 0);
        if (!links) {
            continue;
        }
        const int link_count = static_cast<int>(links->extent());
        for (int link_index = 0; link_index < link_count; ++link_index) {
            const uint32_t link = links->fetch_part(link_index);
            uint32_t slot = link & mask;
            GrindPoint* other = &core::at(node_run(), entry(slot));
            while (other->label() != link) {
                slot = (slot + 1) & mask;
                other = &core::at(node_run(), entry(slot));
            }
            if (!node->succ() || ((other->flag_bits() & kRailFlagDefaultLine) != 0U)) {
                node->set_succ(other);
                box_of(&node->at_point(), &other->at_point(), &node->lower_limit(), &node->upper_limit());
            }
            if (!other->last_value() || ((node->flag_bits() & kRailFlagDefaultLine) != 0U)) {
                other->last_value() = node;
            }
        }
    }
    memory::free_malloc(table_memory);
}

// 0x0057E2D0
bool GrindRailHub::find_grind_rail(const maths::Vec4* start, const maths::Vec4* end, maths::Vec4* point,
                                   GrindPoint** found, const GrindPoint* exclude, RailSearch rail_search) const
{
    const auto min_alignment = rail_search.min_alignment_;
    const auto side = rail_search.side;
    maths::Seg line{};
    line.start = *start;
    line.end = *end;
    maths::Vec4 min;
    maths::Vec4 max;
    box_of(&line.start, &line.end, &min, &max);
    const float snap = script::fetch_float(kRailMaxSnap);
    min.x = static_cast<float>(static_cast<double>(min.x) - snap);
    float best = kNearestStart;
    GrindPoint* best_node = nullptr;
    bool any = false;
    min.y = static_cast<float>(static_cast<double>(min.y) - snap);
    min.z = static_cast<float>(static_cast<double>(min.z) - snap);
    max.x = static_cast<float>(static_cast<double>(max.x) + snap);
    max.y = static_cast<float>(static_cast<double>(max.y) + snap);
    max.z = static_cast<float>(static_cast<double>(max.z) + snap);
    maths::Vec4 best_point;
    for (int index = 0; index < num_; ++index) {
        GrindPoint* node = &core::at(node_run(), index);
        if (((node->flag_bits() & kRailFlagClimbing) != 0U) || node == exclude ||
            ((node->flag_bits() & kRailFlagActive) == 0U)) {
            continue;
        }
        // (the tests as the original's: NaN passes them)
        const bool outside = min.x > node->upper_limit().x || max.x < node->lower_limit().x ||
                             min.z > node->upper_limit().z || max.z < node->lower_limit().z ||
                             min.y > node->upper_limit().y || max.y < node->lower_limit().y;
        if (const GrindPoint* next = node->succ()) {
            if (outside || !rail_clear(node->at_point(), next->at_point())) {
                continue;
            }
            maths::Seg rail{};
            rail.start = node->at_point();
            rail.end = next->at_point();
            maths::Vec4 on_line;
            maths::Vec4 on_rail;
            float along_line = 0.0F;
            float along_rail = 0.0F;
            if (!maths::closest_points_between_lines(line, rail, on_line, on_rail, along_line, along_rail, true)) {
                continue;
            }
            const float distance = (on_rail - on_line).len_of();
            // How well the rail lines up with the line (flat).
            maths::Vec4 rail_dir = next->at_point() - node->at_point();
            rail_dir.y = 0.0F;
            maths::Vec4 seg_direction = *end - *start;
            seg_direction.y = 0.0F;
            rail_dir.make_unit();
            seg_direction.make_unit();
            auto alignment = static_cast<float>(std::fabs((static_cast<double>(seg_direction.x) * rail_dir.x) +
                                                          (static_cast<double>(seg_direction.z) * rail_dir.z) +
                                                          (static_cast<double>(seg_direction.y) * rail_dir.y)));
            if (seg_direction.x == 0.0F && seg_direction.z == 0.0F && alignment == 0.0F) {
                alignment = 1.0F;
            }
            auto score = static_cast<float>((kAlignmentBias - static_cast<double>(alignment)) * distance);
            auto reach = static_cast<float>((kAlignmentReach - alignment) * distance);
            if (side != 0) {
                const maths::Vec4 direction = *end - *start;
                if (node->get_wall_side(&direction) != side) {
                    score = score + score;
                }
            }
            if (!(score < best)) {
                continue;
            }
            if (min_alignment != 1.0F && std::fabs(alignment) < min_alignment) {
                continue;
            }
            best = score;
            if (reach > snap) { // (out of reach: but it still sets the best - and clears what was found)
                any = false;
                continue;
            }
            best_point = on_rail;
            best_node = node;
            any = true;
        } else if (!node->last_value()) {
            // A rail of one node: its point.
            if (outside) {
                continue;
            }
            maths::Vec4 offset = node->at_point() - *start;
            maths::Vec4 direction = *end - *start;
            const maths::Vec4& unit = direction.make_unit();
            const maths::Vec4 along = unit;
            const double projection = (static_cast<double>(offset.x) * along.x) +
                                      (static_cast<double>(offset.z) * along.z) +
                                      (static_cast<double>(offset.y) * along.y);
            offset -= static_cast<float>(projection) * along;
            const double distance =
                std::sqrt((static_cast<double>(offset.x) * offset.x) + (static_cast<double>(offset.y) * offset.y) +
                          (static_cast<double>(offset.z) * offset.z));
            if (distance > snap) {
                continue;
            }
            const double twice = distance + distance;
            if (best < twice) {
                continue;
            }
            best_point = node->at_point();
            best = static_cast<float>(twice);
            best_node = node;
            any = true;
        }
    }
    if (any) {
        *point = best_point;
        *found = best_node;
    }
    return any;
}

// 0x0057CC10
GrindRailHub::GrindRailHub() = default;

} // namespace objects
