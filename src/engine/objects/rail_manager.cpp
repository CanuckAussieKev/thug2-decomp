// The rail manager, 0x0057C170-0x0057EE65. See rail_manager.h.

#include "engine/objects/rail_manager.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/quat_rotation.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/smtptr.h"
#include "engine/objects/trick_objects.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "game/skate/skate_module.h"
#include "game/rider/walk_physics.h"
#include "game/park_editing/park_editor.h"

namespace objects {

namespace {

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
double span4(float a, float b, float c, float d)
{
    double low = a;
    if (b < a)
        low = b;
    if (c < low)
        low = c;
    if (d < low)
        low = d;
    double high = a;
    if (b > a)
        high = b;
    if (c > high)
        high = c;
    if (d > high)
        return d - low;
    return high - low;
}

// 0x0057C200
bool GrindPoint::is_named(uint32_t name) { return label_ == name; }

// 0x0057C210
void BoxPair::get_exchange()
{
    maths::Vec4 held = second;
    second = first;
    first = held;
}

// 0x0057C260
void box_of(const maths::Vec4* a, const maths::Vec4* b, maths::Vec4* min, maths::Vec4* max)
{
    float min_x, max_x, min_y, max_y, min_z, max_z;
    if (a->x < b->x)
        min_x = a->x, max_x = b->x;
    else
        min_x = b->x, max_x = a->x;
    if (a->y < b->y)
        min_y = a->y, max_y = b->y;
    else
        min_y = b->y, max_y = a->y;
    if (a->z < b->z)
        min_z = a->z, max_z = b->z;
    else
        min_z = b->z, max_z = a->z;
    min->x = min_x;
    min->y = min_y;
    min->w = 1.0f;
    min->z = min_z;
    max->y = max_y;
    max->x = max_x;
    max->w = 1.0f;
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
double get_overlap(float a, float b, float c, float d)
{
    auto lengths = static_cast<float>(std::fabs(static_cast<double>(d) - c) + std::fabs(static_cast<double>(b) - a));
    return lengths - span4(a, b, c, d);
}

// 0x0057C3A0
int nearly_equal4(float a, float b, float c, float d, float tolerance)
{
    if (std::fabs(static_cast<double>(a) - b) < tolerance && std::fabs(static_cast<double>(b) - c) < tolerance &&
        std::fabs(static_cast<double>(c) - d) < tolerance)
        return 1;
    return 0;
}

// 0x0057C3F0
void GrindRailHub::move_node(uint32_t name, const maths::Vec4* pos)
{
    int index = 0;
    GrindPoint* node = nodes_;
    for (; index < num_; ++index, ++node) {
        if (node->label_ == name)
            break;
    }
    if (index >= num_)
        return;
    node->at_point_ = *pos;
    if (GrindPoint* next = node->succ_) {
        maths::Vec4 here = node->at_point_;
        maths::Vec4 there = next->at_point_;
        box_of(&here, &there, &node->lower_limit_, &node->upper_limit_);
    }
    if (node->last_value_) {
        // (the original tests the found node's own link each time, not each node's)
        for (int i = 0; i < num_; ++i) {
            GrindPoint* each = &nodes_[index];
            if (each->succ_ == node)
                box_of(&each->at_point_, &node->at_point_, &each->lower_limit_, &each->upper_limit_);
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
Record24* Record24::get_assign(const Record24* other)
{
    for (int i = 0; i < 9; ++i)
        words[i] = other->words[i];
    return this;
}

// 0x0057C540
Record34* Record34::get_assign(const Record34* other)
{
    for (int i = 0; i < 13; ++i)
        words[i] = other->words[i];
    return this;
}

// 0x0057C5A0
bool nodes_together(const GrindPoint* a, const GrindPoint* b)
{
    // (NaN counts as near: the original only fails on "greater")
    return !(std::fabs(static_cast<double>(a->at_point_.x) - b->at_point_.x) > 1.0) &&
           !(std::fabs(static_cast<double>(a->at_point_.y) - b->at_point_.y) > 1.0) &&
           !(std::fabs(static_cast<double>(a->at_point_.z) - b->at_point_.z) > 1.0);
}

namespace {
RailClearOverride g_rail_clear_override; // (NOT ORIGINAL: set_rail_clear_override)
} // namespace

void set_rail_clear_override(RailClearOverride override) { g_rail_clear_override = override; }

// 0x0057C600
bool rail_clear(maths::Vec4 a, maths::Vec4 b)
{
    if (park_editing::g_park_editor->cur_phase_ == 0xD443A2BC) // (the park editor's)
        return true;
    if (g_rail_clear_override && g_rail_clear_override(&a, &b)) // (NOT ORIGINAL: the level editor's flat-ground rails)
        return true;
    render::ProbeRay feeler; // (what it hit let go of by its destructor, where the original lets go inline)
    // A quarter of the way from a to b, 6 above (the original's rounding: some sums kept unrounded).
    double sum_w = static_cast<double>(b.w) + a.w;
    double sum_z = static_cast<double>(b.z) + a.z;
    auto sum_y = static_cast<float>(static_cast<double>(b.y) + a.y);
    auto sum_x = static_cast<float>(static_cast<double>(b.x) + a.x);
    auto half_w = static_cast<float>(sum_w * 0.5);
    double half_z = sum_z * 0.5;
    double half_y = sum_y * 0.5;
    auto half_x = static_cast<float>(sum_x * 0.5);
    auto to_w = static_cast<float>(static_cast<double>(half_w) - a.w);
    auto to_z = static_cast<float>(half_z - a.z);
    double to_y = half_y - a.y;
    double to_x = static_cast<double>(half_x) - a.x;
    auto quarter_w = static_cast<float>(to_w * 0.5);
    auto quarter_z = static_cast<float>(to_z * 0.5);
    auto quarter_y = static_cast<float>(to_y * 0.5);
    double quarter_x = to_x * 0.5;
    auto at_w = static_cast<float>(static_cast<double>(a.w) + quarter_w);
    auto at_z = static_cast<float>(static_cast<double>(a.z) + quarter_z);
    double at_y = static_cast<double>(a.y) + quarter_y;
    auto at_x = static_cast<float>(static_cast<double>(a.x) + quarter_x);
    auto high_y = static_cast<float>(at_y + 6.0);
    // Across the rail (horizontally), 7 long (0x0042BBF0: the length set).
    maths::Vec4 along = {static_cast<float>(static_cast<double>(b.x) - a.x), 0.0f,
                         static_cast<float>(static_cast<double>(b.z) - a.z),
                         static_cast<float>(static_cast<double>(b.w) - a.w)};
    along.set_length(7.0f);
    const float across_x = along.z, across_y = along.y, across_z = static_cast<float>(-static_cast<double>(along.x)),
                across_w = along.w;
    auto sub = [](float p, float q) { return static_cast<float>(static_cast<double>(p) - q); };
    auto add = [](float p, float q) { return static_cast<float>(static_cast<double>(p) + q); };
    const maths::Vec4 minus = {sub(at_x, across_x), sub(high_y, across_y), sub(at_z, across_z), sub(at_w, across_w)};
    const maths::Vec4 plus = {add(across_x, at_x), add(across_y, high_y), add(across_z, at_z), add(across_w, at_w)};
    // Anything across it (either way): not clear.
    feeler.change_strand(&plus, &minus);
    if (feeler.get_hit_shape(true, false)) {
        feeler.entity_.get_release();
        return false;
    }
    reinterpret_cast<BoxPair*>(&feeler)->get_exchange();
    if (feeler.get_hit_shape(true, false)) {
        feeler.entity_.get_release();
        return false;
    }
    // Down 12 from one side: nothing there, clear.
    maths::Vec4 top = plus;
    maths::Vec4 bottom = {plus.x, static_cast<float>(plus.y - 12.0), plus.z, static_cast<float>(plus.w + 1.0)};
    feeler.change_strand(&top, &bottom);
    if (!feeler.get_hit_shape(true, false)) {
        feeler.entity_.get_release();
        return true;
    }
    auto base = static_cast<float>(high_y - 6.0);
    auto height_one = static_cast<float>(static_cast<double>(feeler.contact_pos_.y) - base);
    // And from the other.
    top = minus;
    bottom = {minus.x, static_cast<float>(minus.y - 12.0), minus.z, static_cast<float>(minus.w + 1.0)};
    feeler.change_strand(&top, &bottom);
    if (!feeler.get_hit_shape(true, false)) {
        return true;
    }
    double height_two = static_cast<double>(feeler.contact_pos_.y) - base;
    // Ground on both sides: clear only if one side is at the rail's height (within 1) or 2 below it.
    if (height_one > -1.0f && height_two > -1.0) { // (NaN: on to the next test)
        return false;
    }
    double higher, lower;
    if (height_one < height_two) {
        higher = static_cast<float>(height_two);
        lower = height_one;
    } else {
        higher = height_one;
        lower = height_two;
    }
    if (!(higher > 1.0))
        return true;
    return !(lower > -2.0);
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
        *out = node->at_point_;
        return out;
    }
    maths::Vec4 world;
    xform(&frame_xform_, &world, &node->at_point_);
    *out = world;
    return out;
}

// 0x0057CBD0
GrindPoint* GrindRailHub::find_node(uint32_t name)
{
    if (!nodes_)
        return nullptr;
    for (int i = 0; i < num_; ++i) {
        if (nodes_[i].label_ == name)
            return &nodes_[i];
    }
    return nullptr;
}

// 0x0057CC30
int GrindPoint::get_wall_side(const maths::Vec4* direction)
{
    const maths::Vec4 p = at_point_;
    const maths::Vec4 q = succ_->at_point_;
    render::ProbeRay feeler; // (what it hit let go of by its destructor, where the original lets go inline)
    // A quarter of the way along the segment, 6 below it (the original's rounding: some sums kept unrounded).
    auto sum_y = static_cast<float>(static_cast<double>(q.y) + p.y);
    auto sum_x = static_cast<float>(static_cast<double>(q.x) + p.x);
    auto half_w = static_cast<float>((static_cast<double>(q.w) + p.w) * 0.5);
    double half_z = (static_cast<double>(q.z) + p.z) * 0.5;
    double half_y = sum_y * 0.5;
    auto half_x = static_cast<float>(sum_x * 0.5);
    auto to_w = static_cast<float>(half_w - static_cast<double>(p.w));
    auto to_z = static_cast<float>(half_z - p.z);
    double to_y = half_y - p.y;
    double to_x = static_cast<double>(half_x) - p.x;
    auto quarter_w = static_cast<float>(to_w * 0.5);
    auto quarter_z = static_cast<float>(to_z * 0.5);
    auto quarter_y = static_cast<float>(to_y * 0.5);
    double quarter_x = to_x * 0.5;
    auto at_w = static_cast<float>(static_cast<double>(p.w) + quarter_w);
    auto at_z = static_cast<float>(static_cast<double>(p.z) + quarter_z);
    double at_y = static_cast<double>(p.y) + quarter_y;
    auto at_x = static_cast<float>(quarter_x + p.x);
    auto low_y = static_cast<float>(at_y - 6.0);
    // Across the segment (horizontally), 4 long.
    double dw = static_cast<double>(q.w) - p.w;
    auto dz = static_cast<float>(static_cast<double>(q.z) - p.z);
    double dx = static_cast<double>(q.x) - p.x;
    auto dx_stored = static_cast<float>(dx);
    float across_y = 0.0f;
    float across_x = dz;
    double length = std::sqrt(dx * dx + static_cast<double>(dz) * dz);
    double along_x = dx;
    if (length > 0.0) {
        double scale = 4.0 / length;
        along_x = dx * scale;
        across_y = static_cast<float>(0.0 * scale);
        across_x = static_cast<float>(dz * scale);
    }
    auto across_z = static_cast<float>(-along_x);
    maths::Vec4 from = {static_cast<float>(static_cast<double>(across_x) + at_x),
                        static_cast<float>(static_cast<double>(across_y) + low_y),
                        static_cast<float>(static_cast<double>(across_z) + at_z), static_cast<float>(dw + at_w)};
    maths::Vec4 to = {static_cast<float>(static_cast<double>(at_x) - across_x),
                      static_cast<float>(static_cast<double>(low_y) - across_y),
                      static_cast<float>(static_cast<double>(at_z) - across_z),
                      static_cast<float>(static_cast<double>(at_w) - dw)};
    feeler.change_strand(&from, &to);
    bool hit_one_way = feeler.get_hit_shape(true, false);
    maths::Vec4 start = feeler.line_.start;
    feeler.line_.start = feeler.line_.end;
    feeler.line_.end = start;
    bool hit_other_way = feeler.get_hit_shape(true, false);
    int side = 0;
    if (hit_one_way != hit_other_way) {
        side = hit_one_way ? -1 : 1;
        double dot = static_cast<double>(dx_stored) * direction->x +
                     ((static_cast<double>(q.y) - p.y) * direction->y + static_cast<double>(dz) * direction->z);
        if (dot < 0.0)
            side = -side;
    }
    feeler.entity_.get_release();
    return side;
}

// 0x0057CFB0
maths::Mat4* GrindRailHub::get_climb_matrix(maths::Mat4* out, const GrindPoint* node)
{
    maths::Mat4 local;
    reinterpret_cast<const maths::QuatRotation*>(&node->climb_)->fetch_mat(&local); // the quaternion's
    local.row[0].w = 0.0f;
    local.row[1].w = 0.0f;
    local.row[2].w = 0.0f;
    local.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
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
void GrindRailHub::append_vertex(uint32_t name, script::Struct* params)
{
    GrindPoint* node = &nodes_[adding_];
    uint32_t node_class = 0; // (the original's are uninitialised: read only if the node has them)
    params->read_hash_value(kClass, &node_class, 0);
    bool climbing = node_class == kClimbingNode;
    uint32_t type = 0;
    params->read_hash_value(kType, &type, 0);
    node->succ_ = nullptr;
    node->last_value_ = nullptr;
    node->label_ = name;
    node->flag_bits_ = 0;
    if (params->has_bare_flag(kCreatedAtStart))
        node->flag_bits_ |= kRailFlagActive;
    else
        node->flag_bits_ &= ~kRailFlagActive;
    level_nodes::fetch_place(params, &node->at_point_);
    if (params->has_bare_flag(kLipOverride))
        node->flag_bits_ |= kRailFlagLipOverride;
    if (params->has_bare_flag(kStockRailFlag))
        node->flag_bits_ |= kRailFlagDefaultLine;
    if (climbing) {
        node->flag_bits_ |= kRailFlagClimbing;
        if (type == kLadder)
            node->flag_bits_ |= kRailFlagLadder;
        else
            node->flag_bits_ &= ~kRailFlagLadder;
    }
    auto flag = [&](uint32_t name, uint32_t bit) {
        if (params->has_bare_flag(name))
            node->flag_bits_ |= bit;
        else
            node->flag_bits_ &= ~bit;
    };
    flag(kHangLeft, kRailFlagHangLeft);
    flag(kHangRight, kRailFlagHangRight);
    flag(kNoClimbing, kRailFlagNoClimbing);
    if (climbing) {
        // Its orientation from its angles, as a quaternion.
        maths::Vec4 angles;
        level_nodes::read_turn_angles(params, &angles);
        maths::Mat4 matrix;
        maths::Mat4* built = &matrix.init_from_angles(angles);
        maths::Vec4 quat;
        reinterpret_cast<maths::QuatRotation*>(&quat)->from_matrix(*built);
        node->climb_ = quat;
    } else {
        node->surface_type_ = 0;
        uint32_t terrain = 0; // (the original's: its params argument's slot, the low byte the climbing flag)
        params->read_hash_value(kSurfaceKindKey, &terrain, 1);
        node->surface_type_ = static_cast<uint8_t>(surface_kind::get_terrain_index(terrain));
    }
    box_of(&node->at_point_, &node->at_point_, &node->lower_limit_, &node->upper_limit_);
}

// 0x0057D260
void GrindRailHub::remove_overlaps()
{
    debug::printf("Starting overlapping rail removal\n");
    int removed = 0;
    for (int i = 0; i < num_; ++i) {
        GrindPoint* a = &nodes_[i];
        GrindPoint* a_next = a->succ_;
        if (!a_next)
            continue;
        const maths::Vec4 p = a->at_point_;
        const maths::Vec4 q = a_next->at_point_;
        // Its direction (each step stored as a float, as the original), made unit length.
        auto dx = static_cast<float>(static_cast<double>(q.x) - p.x);
        auto dy = static_cast<float>(static_cast<double>(q.y) - p.y);
        auto dz = static_cast<float>(static_cast<double>(q.z) - p.z);
        const float raw_x = dx, raw_y = dy, raw_z = dz;
        double length =
            std::sqrt(static_cast<double>(dy) * dy + static_cast<double>(dx) * dx + static_cast<double>(dz) * dz);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            dx = static_cast<float>(dx * inverse);
            dy = static_cast<float>(dy * inverse);
            dz = static_cast<float>(dz * inverse);
        }
        // Its box, 16 bigger each way.
        const auto lo_x = static_cast<float>(a->lower_limit_.x - 16.0),
                   lo_y = static_cast<float>(a->lower_limit_.y - 16.0),
                   lo_z = static_cast<float>(a->lower_limit_.z - 16.0);
        const auto hi_x = static_cast<float>(a->upper_limit_.x + 16.0),
                   hi_y = static_cast<float>(a->upper_limit_.y + 16.0),
                   hi_z = static_cast<float>(a->upper_limit_.z + 16.0);
        for (int j = i + 1; j < num_; ++j) {
            if (!(a->flag_bits_ & kRailFlagActive))
                break;
            GrindPoint* b = &nodes_[j];
            GrindPoint* b_next = b->succ_;
            if (!b_next)
                continue;
            // (the tests as the original's: NaN passes them)
            if (b->lower_limit_.x > hi_x || b->upper_limit_.x < lo_x || b->lower_limit_.z > hi_z ||
                b->upper_limit_.z < lo_z || b->lower_limit_.y > hi_y || b->upper_limit_.y < lo_y)
                continue;
            const maths::Vec4 r = b->at_point_;
            const maths::Vec4 s = b_next->at_point_;
            maths::Vec4 along = {
                static_cast<float>(static_cast<double>(s.x) - r.x), static_cast<float>(static_cast<double>(s.y) - r.y),
                static_cast<float>(static_cast<double>(s.z) - r.z), static_cast<float>(static_cast<double>(s.w) - r.w)};
            along.make_unit();
            double dot = static_cast<double>(along.y) * dy + static_cast<double>(along.x) * dx +
                         static_cast<double>(along.z) * dz;
            // Parallel (or opposite) to within about 8 degrees.
            if (!(dot < -0.99f) && !(dot > 0.99f))
                continue;
            maths::Vec4 offset = {
                static_cast<float>(static_cast<double>(r.x) - p.x), static_cast<float>(static_cast<double>(r.y) - p.y),
                static_cast<float>(static_cast<double>(r.z) - p.z), static_cast<float>(static_cast<double>(r.w) - p.w)};
            maths::Vec4 direction = {raw_x, raw_y, raw_z, static_cast<float>(static_cast<double>(q.w) - p.w)};
            direction.make_unit();
            maths::Vec4 apart = offset;
            apart.flatten_onto_plane(direction);
            double distance =
                std::sqrt(static_cast<double>(apart.x) * apart.x + static_cast<double>(apart.z) * apart.z +
                          static_cast<double>(apart.y) * apart.y);
            if (!(distance < 6.0))
                continue;
            int matches = get_overlap(p.x, q.x, r.x, s.x) > 6.0 ? 1 : 0;
            if (get_overlap(p.z, q.z, r.z, s.z) > 6.0)
                ++matches;
            int same = nearly_equal4(p.x, q.x, r.x, s.x, 6.0f) ? 1 : 0;
            if (nearly_equal4(p.z, q.z, r.z, s.z, 6.0f))
                ++same;
            if (same + matches < 2)
                continue;
            // Overlapping: the lower one goes.
            ++removed;
            if (a->at_point_.y < b->at_point_.y)
                a->flag_bits_ &= ~kRailFlagActive;
            else
                b->flag_bits_ &= ~kRailFlagActive;
            break;
        }
    }
    debug::printf("Done overlapping rail removal, removed %d\n", removed);
}

// 0x0057D7A0
void GrindRailHub::remove_overlaps_if_not_park()
{
    if (park_editing::g_park_editor->cur_phase_ == 0xD443A2BC) // (the park editor's level)
        return;
    remove_overlaps();
}

// 0x0057D7C0
void GrindRailHub::change_live(uint32_t name, int active, bool entire_rail)
{
    int index = 0;
    GrindPoint* node = nodes_;
    for (; index < num_; ++index, ++node) {
        if (node->label_ == name)
            break;
    }
    if (index >= num_)
        return;
    if (entire_rail) {
        // This node and every one after it.
        for (; node; node = node->succ_) {
            if (active)
                node->flag_bits_ |= kRailFlagActive;
            else
                node->flag_bits_ &= ~kRailFlagActive;
        }
        return;
    }
    if (active)
        node->flag_bits_ |= kRailFlagActive;
    else
        node->flag_bits_ &= ~kRailFlagActive;
}

// 0x0057D840
bool GrindRailHub::is_operative(uint32_t name)
{
    for (int index = 0; index < num_; ++index) {
        if (nodes_[index].label_ == name)
            return (nodes_[index].flag_bits_ & kRailFlagActive) != 0;
    }
    return false;
}

// 0x0057D880
bool GrindRailHub::find_ladder(const maths::Vec4* pos, float radius, float height, bool up, void* checker,
                               uint32_t extra, GrindPoint** found)
{
    auto radius_squared = static_cast<float>(static_cast<double>(radius) * radius);
    for (int index = num_; index != 0;) {
        GrindPoint* node = &nodes_[--index];
        if (!(node->flag_bits_ & kRailFlagLadder) || !(node->flag_bits_ & kRailFlagActive))
            continue;
        if (std::fabs(static_cast<double>(node->at_point_.x) - pos->x) > radius ||
            std::fabs(static_cast<double>(node->at_point_.z) - pos->z) > radius ||
            std::fabs(static_cast<double>(node->at_point_.y) - pos->y) > height)
            continue;
        double dx = static_cast<double>(node->at_point_.x) - pos->x;
        double dz = static_cast<double>(node->at_point_.z) - pos->z;
        if (dz * dz + dx * dx > radius_squared)
            continue;
        GrindPoint* other = node->succ_ ? node->succ_ : node->last_value_;
        maths::Mat4 matrix;
        if (up) {
            maths::Mat4* orientation = get_climb_matrix(&matrix, node);
            if (static_cast<FootMovement*>(checker)->check_ladder_bottom(
                    &node->at_point_, &other->at_point_, orientation, reinterpret_cast<Record24*>(extra))) {
                *found = node;
                return true;
            }
        } else {
            maths::Mat4* orientation = get_climb_matrix(&matrix, other);
            if (static_cast<FootMovement*>(checker)->check_ladder_top(&other->at_point_, &node->at_point_, orientation,
                                                                      reinterpret_cast<Record24*>(extra))) {
                *found = other;
                return true;
            }
        }
    }
    return false;
}

// 0x0057DA20
bool GrindRailHub::find_ladder_on_line(const maths::Vec4* start, const maths::Vec4* end, void* checker,
                                       Record24* result, GrindPoint** found)
{
    *found = nullptr;
    maths::Seg line;
    line.start = *start;
    line.end = *end;
    maths::Vec4 min, max;
    box_of(&line.start, &line.end, &min, &max);
    double snap = script::fetch_float(kClimbMaxSnap);
    min.x = static_cast<float>(min.x - snap);
    float best = 1e14f;
    min.y = static_cast<float>(min.y - snap);
    min.z = static_cast<float>(min.z - snap);
    max.x = static_cast<float>(max.x + snap);
    max.y = static_cast<float>(max.y + snap);
    max.z = static_cast<float>(max.z + snap);
    auto snap_squared = static_cast<float>(snap * snap);
    for (int index = 0; index < num_; ++index) {
        GrindPoint* node = &nodes_[index];
        if (!(node->flag_bits_ & kRailFlagLadder) || !(node->flag_bits_ & kRailFlagActive) || !node->succ_)
            continue;
        // (the tests as the original's: NaN passes them)
        if (min.x > node->upper_limit_.x || max.x < node->lower_limit_.x || min.z > node->upper_limit_.z ||
            max.z < node->lower_limit_.z || min.y > node->upper_limit_.y || max.y < node->lower_limit_.y)
            continue;
        GrindPoint* next = node->succ_;
        maths::Seg rail;
        rail.start = node->at_point_;
        rail.end = next->at_point_;
        maths::Vec4 on_line, on_rail;
        float s, t;
        if (!maths::closest_points_between_lines(line, rail, on_line, on_rail, s, t, true))
            continue;
        maths::Vec4 apart = on_rail - on_line;
        float distance_squared = apart.magnitude_squared();
        if (node->label_ == kLadderTrigger || next->label_ == kLadderTrigger)
            distance_squared = static_cast<float>(distance_squared * 4.0);
        if (distance_squared > snap_squared || distance_squared > best)
            continue;
        Record24 record;
        maths::Mat4 matrix;
        if (node->at_point_.y < next->at_point_.y) {
            // Climbing up from this node.
            maths::Mat4* orientation = get_climb_matrix(&matrix, node);
            if (!static_cast<FootMovement*>(checker)->check_ladder_line(&node->at_point_, &next->at_point_, orientation,
                                                                        &on_rail, t, &record))
                continue;
            *found = node;
        } else {
            maths::Mat4* orientation = get_climb_matrix(&matrix, next);
            if (!static_cast<FootMovement*>(checker)->check_ladder_line(&next->at_point_, &node->at_point_, orientation,
                                                                        &on_rail, static_cast<float>(1.0 - t), &record))
                continue;
            *found = next;
        }
        best = distance_squared;
        result->get_assign(&record);
    }
    return *found != nullptr;
}

// 0x0057DDA0
bool GrindRailHub::find_rail(const maths::Vec4* start, const maths::Vec4* end, const maths::Vec4* direction,
                             void* checker, Record34* result, float margin)
{
    bool found = false;
    maths::Seg line;
    line.start = *start;
    line.end = *end;
    maths::Vec4 min, max;
    box_of(&line.start, &line.end, &min, &max);
    min.x = static_cast<float>(static_cast<double>(min.x) - margin);
    float best = 1e14f;
    min.y = static_cast<float>(static_cast<double>(min.y) - margin);
    min.z = static_cast<float>(static_cast<double>(min.z) - margin);
    max.x = static_cast<float>(static_cast<double>(max.x) + margin);
    max.y = static_cast<float>(static_cast<double>(max.y) + margin);
    max.z = static_cast<float>(static_cast<double>(max.z) + margin);
    auto margin_squared = static_cast<float>(static_cast<double>(margin) * margin);
    for (int index = 0; index < num_; ++index) {
        GrindPoint* node = &nodes_[index];
        if ((node->flag_bits_ & kRailFlagLadder) || (node->flag_bits_ & kRailFlagNoClimbing) ||
            !(node->flag_bits_ & kRailFlagActive) || !node->succ_)
            continue;
        // (the tests as the original's: NaN passes them)
        if (min.x > node->upper_limit_.x || max.x < node->lower_limit_.x || min.z > node->upper_limit_.z ||
            max.z < node->lower_limit_.z || min.y > node->upper_limit_.y || max.y < node->lower_limit_.y)
            continue;
        GrindPoint* next = node->succ_;
        maths::Seg rail;
        rail.start = node->at_point_;
        rail.end = next->at_point_;
        maths::Vec4 on_line, on_rail;
        float s, t;
        if (!maths::closest_points_between_lines(line, rail, on_line, on_rail, s, t, true))
            continue;
        maths::Vec4 apart = on_rail - on_line;
        if (apart.magnitude_squared() > margin_squared)
            continue;
        // Distance along `direction` counts a third.
        double along = static_cast<double>(apart.z) * direction->z + static_cast<double>(apart.y) * direction->y +
                       static_cast<double>(apart.x) * direction->x;
        if (along > 0.0)
            apart -= static_cast<float>(along * 0.6666667f) * *direction;
        float distance_squared = apart.magnitude_squared();
        if (distance_squared > best)
            continue;
        maths::Vec4 node_world, next_world;
        get_absolute_pos(&next_world, next);
        get_absolute_pos(&node_world, node);
        if (!rail_clear(node_world, next_world))
            continue;
        Record34 record;
        record.words[12] = reinterpret_cast<uint32_t>(node);
        maths::Vec4 line_world, rail_world, next_at, node_at;
        maths::Vec4* line_at = to_world(&line_world, &on_line);
        maths::Vec4* rail_at = to_world(&rail_world, &on_rail);
        maths::Vec4* next_pos = get_absolute_pos(&next_at, next);
        maths::Vec4* node_pos = get_absolute_pos(&node_at, node);
        if (!static_cast<FootMovement*>(checker)->check_rail(node_pos, next_pos, rail_at, line_at, t, &record, false))
            continue;
        best = distance_squared;
        result->get_assign(&record);
        found = true;
    }
    return found;
}

// 0x0057E170
bool GrindRailHub::find_joining_rail(const GrindPoint* from, uint32_t exclude_flags, GrindPoint** found)
{
    for (int index = 0; index < num_; ++index) {
        GrindPoint* node = &nodes_[index];
        // (the tests as the original's: NaN passes them)
        if (std::fabs(static_cast<double>(node->at_point_.x) - from->at_point_.x) > 1.0 ||
            std::fabs(static_cast<double>(node->at_point_.y) - from->at_point_.y) > 1.0 ||
            std::fabs(static_cast<double>(node->at_point_.z) - from->at_point_.z) > 1.0)
            continue;
        GrindPoint* next = node->succ_;
        if (!next && !node->last_value_)
            continue;
        if (node == from)
            continue;
        if (next && (node->flag_bits_ & kRailFlagActive) && !(exclude_flags & node->flag_bits_)) {
            maths::Vec4 node_world, next_world;
            get_absolute_pos(&next_world, next);
            get_absolute_pos(&node_world, node);
            if (rail_clear(node_world, next_world)) {
                *found = node;
                return true;
            }
        }
        GrindPoint* prev = node->last_value_;
        if (prev && (prev->flag_bits_ & kRailFlagActive) && !(exclude_flags & prev->flag_bits_)) {
            maths::Vec4 prev_world, node_world;
            get_absolute_pos(&node_world, node);
            get_absolute_pos(&prev_world, node->last_value_);
            if (rail_clear(prev_world, node_world)) {
                *found = node->last_value_;
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
    for (uint32_t i = 0; i < nodes->extent_; ++i) {
        auto* node = reinterpret_cast<script::Struct*>(nodes->fetch_part(i));
        if (!skipped(node) && is_rail(node))
            ++num_;
    }
    if (!num_)
        return;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->bottom_up_heap_);
    nodes_ = static_cast<GrindPoint*>(memory::alloc_block(num_ * sizeof(GrindPoint)));
    // A hash table of the nodes' names (0x4000 slots, linear probing) to their indexes.
    // NOT ORIGINAL: with 0x2000 rail nodes or more (THP8's whole town has ~18,000; past 0x4000 the original's table
    // was full and adding went round it for ever) a bigger one, 32-bit indexes, at most a quarter full. Original
    // behaviour: 0x4000 16-bit slots whatever the count.
    const bool big = num_ >= 0x2000;
    uint32_t slots = 0x4000;
    while (big && slots < static_cast<uint32_t>(num_) * 4)
        slots <<= 1;
    const uint32_t mask = slots - 1;
    void* table_memory = memory::alloc_block(big ? slots * 4 : 0x8000);
    memory::Hub::get_pull()->leave_heap_scope();
    std::memset(table_memory, 0xFF, big ? slots * 4 : 0x8000);
    auto* small = static_cast<int16_t*>(table_memory);
    auto* wide = static_cast<int32_t*>(table_memory);
    auto entry = [&](uint32_t slot) { return big ? wide[slot] : small[slot]; };
    adding_ = 0;
    bool all_trick_objects = script::fetch_int_value(kAllRailsTrickObjects) != 0;
    for (uint32_t i = 0; i < nodes->extent_; ++i) {
        auto* node = reinterpret_cast<script::Struct*>(nodes->fetch_part(i));
        if (skipped(node) || !is_rail(node))
            continue;
        uint32_t name = level_nodes::get_node_name(node);
        append_vertex(name, node);
        uint32_t slot = name & mask;
        while (entry(slot) != -1)
            slot = (slot + 1) & mask;
        if (big)
            wide[slot] = adding_;
        else
            small[slot] = static_cast<int16_t>(adding_);
        bool trick_object = node->has_entry_named(kTrickObject);
        if (all_trick_objects || trick_object) {
            // A trick object of its cluster (the park / trick object manager 0x007CE500).
            uint32_t node_name = 0;
            node->read_hash_value(kName, &node_name, 1);
            uint32_t cluster = node_name;
            node->read_hash_value(kCluster, &cluster, 1);
            auto* trick_objects = g_trick_spot_hub;
            trick_objects->add_cluster(cluster);
            trick_objects->add_object_to_cluster(node_name, cluster);
        }
        ++adding_;
    }
    // The links: each node's Links (by name, through the table).
    for (int i = 0; i < num_; ++i) {
        GrindPoint* node = &nodes_[i];
        script::Struct* params = level_nodes::fetch_vertex(node->label_, true);
        script::Array* links = nullptr;
        params->fetch_list(kLinks, &links, 0);
        if (!links)
            continue;
        int link_count = static_cast<int>(links->extent_);
        for (int l = 0; l < link_count; ++l) {
            uint32_t link = links->fetch_part(l);
            uint32_t slot = link & mask;
            GrindPoint* other = &nodes_[entry(slot)];
            while (other->label_ != link) {
                slot = (slot + 1) & mask;
                other = &nodes_[entry(slot)];
            }
            if (!node->succ_ || (other->flag_bits_ & kRailFlagDefaultLine)) {
                node->succ_ = other;
                box_of(&node->at_point_, &other->at_point_, &node->lower_limit_, &node->upper_limit_);
            }
            if (!other->last_value_ || (node->flag_bits_ & kRailFlagDefaultLine))
                other->last_value_ = node;
        }
    }
    memory::free_malloc(table_memory);
}

// 0x0057E2D0
bool GrindRailHub::find_grind_rail(const maths::Vec4* start, const maths::Vec4* end, maths::Vec4* point,
                                   GrindPoint** found, const GrindPoint* exclude, float min_alignment, int side)
{
    maths::Seg line;
    line.start = *start;
    line.end = *end;
    maths::Vec4 min, max;
    box_of(&line.start, &line.end, &min, &max);
    float snap = script::fetch_float(kRailMaxSnap);
    min.x = static_cast<float>(static_cast<double>(min.x) - snap);
    float best = 1e7f;
    GrindPoint* best_node = nullptr;
    bool any = false;
    min.y = static_cast<float>(static_cast<double>(min.y) - snap);
    min.z = static_cast<float>(static_cast<double>(min.z) - snap);
    max.x = static_cast<float>(static_cast<double>(max.x) + snap);
    max.y = static_cast<float>(static_cast<double>(max.y) + snap);
    max.z = static_cast<float>(static_cast<double>(max.z) + snap);
    maths::Vec4 best_point;
    for (int index = 0; index < num_; ++index) {
        GrindPoint* node = &nodes_[index];
        if ((node->flag_bits_ & kRailFlagClimbing) || node == exclude || !(node->flag_bits_ & kRailFlagActive))
            continue;
        // (the tests as the original's: NaN passes them)
        bool outside = min.x > node->upper_limit_.x || max.x < node->lower_limit_.x || min.z > node->upper_limit_.z ||
                       max.z < node->lower_limit_.z || min.y > node->upper_limit_.y || max.y < node->lower_limit_.y;
        if (GrindPoint* next = node->succ_) {
            if (outside || !rail_clear(node->at_point_, next->at_point_))
                continue;
            maths::Seg rail;
            rail.start = node->at_point_;
            rail.end = next->at_point_;
            maths::Vec4 on_line, on_rail;
            float s, t;
            if (!maths::closest_points_between_lines(line, rail, on_line, on_rail, s, t, true))
                continue;
            float distance = (on_rail - on_line).len_of();
            // How well the rail lines up with the line (flat).
            maths::Vec4 rail_dir = next->at_point_ - node->at_point_;
            rail_dir.y = 0.0f;
            maths::Vec4 seg_direction = *end - *start;
            seg_direction.y = 0.0f;
            rail_dir.make_unit();
            seg_direction.make_unit();
            auto alignment = static_cast<float>(std::fabs(static_cast<double>(seg_direction.x) * rail_dir.x +
                                                          static_cast<double>(seg_direction.z) * rail_dir.z +
                                                          static_cast<double>(seg_direction.y) * rail_dir.y));
            if (seg_direction.x == 0.0f && seg_direction.z == 0.0f && alignment == 0.0f)
                alignment = 1.0f;
            auto score = static_cast<float>((1.122f - static_cast<double>(alignment)) * distance);
            auto reach = static_cast<float>((2.0 - alignment) * distance);
            if (side) {
                maths::Vec4 direction = *end - *start;
                if (node->get_wall_side(&direction) != side)
                    score = score + score;
            }
            if (!(score < best))
                continue;
            if (min_alignment != 1.0f && std::fabs(alignment) < min_alignment)
                continue;
            best = score;
            if (reach > snap) { // (out of reach: but it still sets the best - and clears what was found)
                any = false;
                continue;
            }
            best_point = on_rail;
            best_node = node;
            any = true;
        } else if (!node->last_value_) {
            // A rail of one node: its point.
            if (outside)
                continue;
            maths::Vec4 offset = node->at_point_ - *start;
            maths::Vec4 direction = *end - *start;
            maths::Vec4& unit = direction.make_unit();
            maths::Vec4 along = unit;
            double k = static_cast<double>(offset.x) * along.x + static_cast<double>(offset.z) * along.z +
                       static_cast<double>(offset.y) * along.y;
            offset -= static_cast<float>(k) * along;
            double distance =
                std::sqrt(static_cast<double>(offset.x) * offset.x + static_cast<double>(offset.y) * offset.y +
                          static_cast<double>(offset.z) * offset.z);
            if (distance > snap)
                continue;
            double twice = distance + distance;
            if (best < twice)
                continue;
            best_point = node->at_point_;
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
GrindRailHub::GrindRailHub()
{
    nodes_ = nullptr;
    num_ = 0;
    on_object_ = false;
    node_array_ = nullptr;
}

} // namespace objects
