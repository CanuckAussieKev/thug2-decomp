// The park's rail sets, strings and points (0x0058AFC0-0x0058B7D5). See park_rails.h.

#include "game/park_editing/park_rails.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/slab_bank.h"
#include "engine/maths/vec4.h"
#include "engine/core/no_destroy.h"
#include "engine/core/checksums.h"

namespace park_editing {

namespace {

constexpr std::size_t kPointNameSize = 64;
// NOT ORIGINAL grouping - park_rails.cpp's state (the original's globals). Made on first use, never destroyed
// (park_rails_state()).
struct ParkRailsState {
    memory::PoolPair park_string_pools_{};
    memory::PoolPair park_point_pools_{};
};

ParkRailsState& park_rails_state()
{
    static core::NoDestroy<ParkRailsState> state;
    return state.get();
}

// The classes' pool pairs (CPoolable): a set's pool is lent to them while one of its items is made or freed.
memory::PoolPair& string_pools() { return park_rails_state().park_string_pools_; }
memory::PoolPair& point_pools() { return park_rails_state().park_point_pools_; }

// Whether the pair's current pool was made by it (and so is deleted when it's taken away): a byte per pool at +4.
uint8_t& pool_owned(memory::PoolPair& pair) { return core::at(pair.made_here(), pair.current()); }

// A pool deleted: its block (0x005BA4E0, ~Pool), then the pool.
void delete_pool(memory::SlabBank* pool)
{
    memory::dealloc(pool->memory().data());
    memory::Hub::get_pull()->dealloc(pool);
}

// `pool` lent to `pair` as its current pool (not owned).
void lend_pool(memory::PoolPair& pair, memory::SlabBank* pool)
{
    core::at(pair.pools(), pair.current()) = pool;
    pool_owned(pair) = 0;
}
// The current pool taken away again (deleted if the pair made it).
void take_back_pool(memory::PoolPair& pair)
{
    if (memory::SlabBank* pool = core::at(pair.pools(), pair.current())) {
        if (pool_owned(pair))
            delete_pool(pool);
        core::at(pair.pools(), pair.current()) = nullptr;
    }
}

// A climbing string's heading turned by a quarter, half and three-quarter turn (0x00645484, 0x0064F1B8, 0x0064B76C).
const float kQuarterTurn = std::bit_cast<float>(0x3FC90FDBu);
const float kHalfTurn = std::bit_cast<float>(0x40490FDBu);
const float kThreeQuarterTurn = std::bit_cast<float>(0x4096CBE4u);

} // namespace

// 0x0058AFC0
void ParkRailString::append_spot(ParkRailPoint* point)
{
    if (!point_chain_) {
        ++vertex_total_;
        point_chain_ = point;
        tail_node_ = point;
        return;
    }
    tail_node_->following_ = point;
    ++vertex_total_;
    tail_node_ = point;
}

// 0x0058B040
int ParkRailString::count_segments() const
{
    if (flag_bits_ & kLoop)
        return vertex_total_;
    int points = 0;
    for (const ParkRailPoint* point = point_chain_; point; point = point->following_)
        ++points;
    return points > 1 ? points - 1 : 0;
}

// 0x0058B220
void ParkRailString::dealloc(void* item) { string_pools().dealloc(item); }

// 0x0058B420
void ParkRailString::copy_from(const ParkRailString* source, const maths::Vec4* offset, RailCopy rail_copy)
{
    const auto rot = rail_copy.rot;
    const auto cluster = rail_copy.cluster;
    const auto angle = rail_copy.angle;
    const bool climbing = source->class_ == kClimbingNode;
    for (const ParkRailPoint* from = source->point_chain_; from; from = from->following_) {
        ParkRailPoint* point = point_group_->append_rail_vertex(); // (not null-checked: as the original)
        std::memcpy(point, from, offsetof(ParkRailPoint, following_));
        // Turned about y: what's added to the offset's x and z.
        float x = 0.0f, z = 0.0f;
        switch (rot) {
        case 1:
            x = from->at_point_.z;
            z = -from->at_point_.x;
            if (climbing)
                point->headings_.y = angle + kQuarterTurn;
            break;
        case 2:
            x = -from->at_point_.x;
            z = -from->at_point_.z;
            if (climbing)
                point->headings_.y = angle + kHalfTurn;
            break;
        case 3:
            x = -from->at_point_.z;
            z = from->at_point_.x;
            if (climbing)
                point->headings_.y = angle + kThreeQuarterTurn;
            break;
        default:
            x = from->at_point_.x;
            z = from->at_point_.z;
            if (climbing)
                point->headings_.y = angle;
            break;
        }
        point->at_point_.w = 1.0f;
        point->at_point_.x = x + offset->x;
        point->at_point_.y = offset->y + from->at_point_.y;
        point->at_point_.z = offset->z + z;
        point->cluster_ = cluster;
        append_spot(point);
    }
    class_ = source->class_;
    label_ = source->label_;
    flag_bits_ =
        static_cast<uint8_t>((flag_bits_ & ~(kLoop | kClimbable)) | (source->flag_bits_ & (kLoop | kClimbable)));
}

// 0x0058B620
void ParkRailString::delete_points()
{
    memory::PoolPair& pair = point_pools();
    for (ParkRailPoint* point = point_chain_; point;) {
        ParkRailPoint* next = point->following_;
        lend_pool(pair, point_group_->point_pool());
        pair.dealloc(point);
        take_back_pool(pair);
        point = next;
    }
    point_chain_ = nullptr;
    tail_node_ = nullptr;
    vertex_total_ = 0;
}

// 0x0058AFF0
ParkRailSet::ParkRailSet() = default;

// 0x0058B010
void ParkRailSet::add_rail(ParkRailString* string)
{
    if (!rail_groups_) {
        ++rail_total_;
        rail_groups_ = string;
        tail_node_ = string;
        return;
    }
    tail_node_->set_following(string);
    ++rail_total_;
    tail_node_ = string;
}

// 0x0058B070
int ParkRailSet::rail_point_total() const
{
    int points = 0;
    for (const ParkRailString* string = rail_groups_; string; string = string->following())
        points += string->vertex_total();
    return points;
}

// 0x0058B090
ParkRailString* ParkRailSet::find_rail(checksums::Name rail_name, int index)
{
    const uint32_t name = rail_name.value();
    int found = 0;
    for (ParkRailString* string = rail_groups_; string; string = string->following()) {
        if (string->label() == name) {
            if (found == index)
                return string;
            ++found;
        }
    }
    return nullptr;
}

// 0x0058B0C0
void ParkRailSet::prepare_all(RailCounts rail_counts, bool in)
{
    const auto points = rail_counts.points;
    const auto strings = rail_counts.strings;
    if (in)
        std::memcpy(label_.data(), "In set", sizeof("In set"));
    else
        std::memcpy(label_.data(), "Out set", sizeof("Out set"));
    std::array<char, kPointNameSize> point_name{};
    std::array<char, kPointNameSize> string_name{};
    text::format_to(point_name, "%s RailPoint", label_.data());
    text::format_to(string_name, "%s RailString", label_.data());
    void* memory = memory::alloc(sizeof(memory::SlabBank));
    point_pool_ =
        memory ? memory::construct_in<memory::SlabBank>(memory, sizeof(ParkRailPoint), points, point_name.data()) : nullptr;
    memory = memory::alloc(sizeof(memory::SlabBank));
    rail_pool_ = memory ? new (memory) memory::SlabBank(sizeof(ParkRailString), strings, string_name.data()) : nullptr;
}

// 0x0058B280
void ParkRailSet::free_pools()
{
    if (rail_pool_) {
        delete_pool(rail_pool_);
        rail_pool_ = nullptr;
    }
    if (point_pool_) {
        delete_pool(point_pool_);
        point_pool_ = nullptr;
    }
}

// 0x0058B2D0
ParkRailPoint* ParkRailSet::append_rail_vertex()
{
    memory::PoolPair& pair = point_pools();
    lend_pool(pair, point_pool_);
    auto* point = pair.allocate<ParkRailPoint>();
    if (point) {
        point->at_point_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
        point->following_ = nullptr;
    }
    take_back_pool(pair);
    return point;
}

// 0x0058B370
ParkRailString* ParkRailSet::get_new_rail()
{
    memory::PoolPair& pair = string_pools();
    lend_pool(pair, rail_pool_);
    auto* string = pair.allocate<ParkRailString>();
    if (string) {
        string->flag_bits() =
            static_cast<uint8_t>((string->flag_bits() & ~ParkRailString::kLoop) | ParkRailString::kClimbable);
        string->set_point_chain(nullptr);
        string->set_vertex_total(0);
        string->set_following(nullptr);
    }
    take_back_pool(pair);
    string->set_point_group(this); // (not null-checked: as the original)
    return string;
}

// 0x0058B5B0
void ParkRailSet::free_rail(ParkRailString* string)
{
    memory::PoolPair& pair = string_pools();
    lend_pool(pair, rail_pool_);
    if (string)
        ParkRailString::dealloc(string);
    take_back_pool(pair);
}

// 0x0058B700
void ParkRailSet::get_clear()
{
    memory::PoolPair& pair = string_pools();
    for (ParkRailString* string = rail_groups_; string;) {
        ParkRailString* next = string->following();
        string->delete_points();
        lend_pool(pair, rail_pool_);
        pair.dealloc(string);
        take_back_pool(pair);
        string = next;
    }
    rail_groups_ = nullptr;
    tail_node_ = nullptr;
    rail_total_ = 0;
}

} // namespace park_editing
