// The park editor's rail editor (rail_editor.h): the rails' and points' lists, pools, packing and the component's life.
// The sectors drawn for them: rail_editor_draw.cpp; the script side: rail_editor_script.cpp.

#include "game/park_editing/rail_editor.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <gsl/pointers>
#include <array>
#include <utility>
#include <gsl/assert>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "formats/byte_stream.h"
#include "formats/rail_buffer_format.h"
#include "game/park_editing/park.h"
#include "game/park_editing/rail_editor_internal.h"
#include "engine/objects/entity_hub.h"
#include "game/park/park_memory.h"
#include "engine/objects/compositemgr.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "game/park_editing/park_editing_state.h"
#include "engine/maths/vec4.h"

namespace park_editing {

namespace rail_editor_internal {

memory::PoolPair& point_pools()
{
    static core::NoDestroy<memory::PoolPair> rail_point_pools;
    return rail_point_pools.get();
}

void remove_scene_sector(render::WorldCell*& sector)
{
    if (!sector) {
        return;
    }
    ParkAssembler* generator = ParkLayout::singleton()->builder();
    generator->remove_scene_sector(sector_key_from_spec(sector), 0);
    set_counts_dirty();
    if (park_editing_state().rail_editor_flag()) {
        ParkLayout::singleton()->builder()->rebuild_collision();
    }
    sector = nullptr;
}

} // namespace rail_editor_internal

using namespace rail_editor_internal;

namespace {

constexpr std::uint32_t kAllVisible = 0xFFFFFFFFU; // every bit of a sector's visibility mask
constexpr float kHeightPackScale = 16.0F;          // a packed height is in sixteenths
constexpr float kPositionPackScale = 8.0F;         // a packed position is in eighths
constexpr float kHeightUnpackScale = 0.0625F;
constexpr float kPositionUnpackScale = 0.125F;
constexpr float kFarthestDistanceSquared = 1e8F; // none is nearer than this squared

uint32_t& next_rail_id()
{
    static uint32_t next_rail_id{1};
    return next_rail_id;
}

constexpr uint32_t kInput = 0x27D7CD28, kEditorCamera = 0xAC8946E2;

// A pool pair's "made here" flags (one byte each, +0x04).
uint8_t& get_owned(memory::PoolPair& pair, int which) { return core::at(pair.made_here(), which); }

// A pool made for a pair's current slot (owned).
void make_pool(memory::PoolPair& pair, int item_size, int count, const char* name)
{
    void* memory = memory::alloc(sizeof(memory::SlabBank));
    core::at(pair.pools(), pair.current()) =
        memory ? memory::construct_in<memory::SlabBank>(memory, item_size, count, name) : nullptr;
    get_owned(pair, pair.current()) = 1;
}

// The current pool freed if it was made here; the slot cleared.
void free_pool(memory::PoolPair& pair)
{
    memory::SlabBank* pool = core::at(pair.pools(), pair.current());
    if (!pool) {
        return;
    }
    if (get_owned(pair, pair.current())) {
        memory::dealloc(pool->memory().data());
        memory::Hub::dealloc(pool);
    }
    core::at(pair.pools(), pair.current()) = nullptr;
}

// A rail taken out of the editor's list (the head and the current rail moved on past it).
void uncouple(RailAuthoring* editor, EditedRail* rail)
{
    if (editor->rail_groups() == rail) {
        editor->set_rail_groups(rail->following());
    }
    if (editor->active_node() == rail) {
        editor->set_active_node(rail->following());
    }
    if (rail->following()) {
        rail->following()->set_preceding(rail->preceding());
    }
    if (rail->preceding()) {
        rail->preceding()->set_following(rail->following());
    }
}

bool get_inside(const EditedRailPoint* p, float x0, float z0, float x1, float z1)
{
    return p->at_point().x > x0 && p->at_point().x < x1 && p->at_point().z > z0 && p->at_point().z < z1;
}

} // namespace

// --- CEditedRailPoint ---------------------------------------------------------------------------------------------

// 0x004F3AD0
void EditedRailPoint::clear_glow()
{
    if (!highlighted_) {
        return;
    }
    auto restore = [](render::WorldCell* sector) {
        clear_sector_colour(sector);
        set_sector_visibility(sector, kAllVisible);
    };
    if (post_) {
        restore(post_);
    }
    if (segment_) {
        restore(segment_);
    }
    if (preceding_ && preceding_->segment_) {
        restore(preceding_->segment_);
    }
    if (following_ && !following_->following_ && following_->post_) {
        restore(following_->post_);
    }
    if (preceding_ && !preceding_->preceding_ && preceding_->post_) {
        restore(preceding_->post_);
    }
    highlighted_ = false;
}

// 0x004F40A0
void EditedRailPoint::delete_segment() { remove_scene_sector(segment_); }

// 0x004F40F0
void EditedRailPoint::delete_post() { remove_scene_sector(post_); }

// 0x004F4140
void EditedRailPoint::get_pack(std::array<uint16_t, kPackedPointWords>& record) const
{
    core::at(record, 4) = has_post_;
    if (level_y_ < 0.0F) {
        core::at(record, 3) = 0;
    } else {
        core::at(record, 3) = static_cast<uint16_t>(static_cast<int>(static_cast<double>(level_y_) * kHeightPackScale));
    }
    core::at(record, 0) = static_cast<uint16_t>(static_cast<int>(static_cast<double>(at_point_.x) * kPositionPackScale));
    core::at(record, 1) = static_cast<uint16_t>(static_cast<int>(static_cast<double>(at_point_.y) * kPositionPackScale));
    core::at(record, 2) = static_cast<uint16_t>(static_cast<int>(static_cast<double>(at_point_.z) * kPositionPackScale));
}

// --- CEditedRail --------------------------------------------------------------------------------------------------

// 0x004F70C0
EditedRail::EditedRail() : ident_(next_rail_id()++) { get_clear(); }

// 0x004F6E20
void EditedRail::get_clear()
{
    EditedRailPoint* point = point_chain_;
    while (point) {
        EditedRailPoint* next = point->following();
        remove_scene_sector(point->segment());
        remove_scene_sector(point->post());
        point_pools().dealloc(point);
        point = next;
    }
    point_chain_ = nullptr;
}

// 0x004F3B90
int EditedRail::rail_point_total() const
{
    int count = 0;
    for (const EditedRailPoint* p = point_chain_; p; p = p->following()) {
        ++count;
    }
    return count;
}

// 0x004F3BB0
EditedRailPoint* EditedRail::fetch_vertex_at(int index) const
{
    EditedRailPoint* p = point_chain_;
    while (index != 0) {
        if (!p) {
            return nullptr;
        }
        p = p->following();
        --index;
    }
    return p;
}

// 0x004F3BD0
void EditedRail::clear_colours()
{
    for (const EditedRailPoint* p = point_chain_; p; p = p->following()) {
        if (p->post()) {
            clear_sector_colour(p->post());
        }
        if (p->segment()) {
            clear_sector_colour(p->segment());
        }
    }
}

// 0x004F41C0
void EditedRail::delete_segments()
{
    for (EditedRailPoint* p = point_chain_; p; p = p->following()) {
        remove_scene_sector(p->segment());
    }
}

// 0x004F4220
void EditedRail::delete_posts()
{
    for (EditedRailPoint* p = point_chain_; p; p = p->following()) {
        remove_scene_sector(p->post());
    }
}

// 0x004F4280
bool EditedRail::find_nearest_point(const maths::Vec4* position, maths::Vec4* point, float* distance_squared,
                                    int* index, int skip) const
{
    const EditedRailPoint* p = point_chain_;
    if (!p) {
        return false;
    }
    float best = kFarthestDistanceSquared;
    for (int i = 0; p; p = p->following(), ++i) {
        if (i == skip) {
            continue;
        }
        const double dx = static_cast<double>(p->at_point().x) - position->x;
        const double dz = static_cast<double>(p->at_point().z) - position->z;
        const double d = (dx * dx) + (dz * dz);
        if (d < best) {
            best = static_cast<float>(d);
            *point = p->at_point();
            *distance_squared = static_cast<float>(d);
            *index = i;
        }
    }
    return true;
}

// 0x004F4310
int EditedRail::count_points_inside(float x0, float z0, float x1, float z1) const
{
    int count = 0;
    for (const EditedRailPoint* p = point_chain_; p; p = p->following()) {
        if (get_inside(p, x0, z0, x1, z1)) {
            ++count;
        }
    }
    return count;
}

// 0x004F4420
void EditedRail::change_colour(int index)
{
    script::Array* colours = script::fetch_list(kGraffitiColors);
    auto* colour = colours->array_at(static_cast<uint32_t>(index));
    const uint32_t rgba = static_cast<uint8_t>(colour->fetch_part(0)) |
                          static_cast<uint8_t>(colour->fetch_part(1)) << 8 |
                          static_cast<uint32_t>(static_cast<uint8_t>(colour->fetch_part(2))) << 16 | 0x80000000U;
    for (const EditedRailPoint* p = point_chain_; p; p = p->following()) {
        if (p->post()) {
            set_sector_colour(p->post(), rgba);
        }
        if (p->segment()) {
            set_sector_colour(p->segment(), rgba);
        }
    }
}

// 0x004F44B0
bool EditedRail::any_point_outside(float x0, float z0, float x1, float z1) const
{
    for (const EditedRailPoint* p = point_chain_; p; p = p->following()) {
        if (p->at_point().x < x0 || p->at_point().x > x1 || p->at_point().z < z0 || p->at_point().z > z1) {
            return true;
        }
    }
    return false;
}

// 0x004F6240
EditedRailPoint* EditedRail::append_rail_vertex()
{
    memory::PoolPair& pools = point_pools();
    if (core::at(pools.pools(), pools.current())->taken_count() == RailAuthoring::kMaxPoints) {
        return nullptr;
    }
    auto* point = pools.allocate<EditedRailPoint>();
    if (point) {
        point->at_point() = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        point->set_has_post(false);
        point->set_highlighted(false);
        point->set_level_y(0.0F);
        point->set_following(nullptr);
        point->set_preceding(nullptr);
        point->set_segment(nullptr);
        point->set_post(nullptr);
    }
    // (a failed allocation crashes here, as in the original)
    point->set_following(point_chain_);
    if (point_chain_) {
        point_chain_->set_preceding(point);
    }
    point->set_preceding(nullptr);
    point_chain_ = point;
    return point;
}

// 0x004F62D0
void EditedRail::add_point_copy(const EditedRailPoint* source)
{
    EditedRailPoint* point = append_rail_vertex();
    point->at_point() = source->at_point();
    point->set_has_post(source->has_post());
    point->set_level_y(source->level_y());
}

// 0x004F6300
EditedRailPoint* EditedRail::cut_at(EditedRailPoint* point)
{
    if (!point) {
        return nullptr;
    }
    EditedRailPoint* prev = point->preceding();
    if (prev && point->following()) {
        const memory::PoolPair& rails = park_editing_state().rail_pools();
        if (core::at(rails.pools(), rails.current())->taken_count() == RailAuthoring::kMaxRails) {
            return nullptr;
        }
    }
    if (point == point_chain_) {
        point_chain_ = point->following();
        if (point->following()) {
            point->following()->set_preceding(nullptr);
        }
        point->delete_segment();
        point->delete_post();
        RailAuthoring::free_point(point);
        return nullptr;
    }
    prev->delete_segment();
    point->preceding()->set_following(nullptr);
    EditedRailPoint* rest = point->following();
    point->delete_segment();
    point->delete_post();
    RailAuthoring::free_point(point);
    if (rest) {
        rest->set_preceding(nullptr);
    }
    return rest;
}

// 0x004F6F40
void EditedRail::copy_from(const EditedRail* source)
{
    get_clear();
    for (const EditedRailPoint* p = source->point_chain_; p; p = p->following()) {
        add_point_copy(p);
    }
}

// 0x004F7100
EditedRail* EditedRail::copy_inside(float x0, float z0, float x1, float z1, EditedRail* list) const
{
    bool last_inside = false;
    const EditedRailPoint* first = nullptr; // an inside point waiting for a second
    for (const EditedRailPoint* p = point_chain_; p; p = p->following()) {
        if (!get_inside(p, x0, z0, x1, z1)) {
            first = nullptr;
            last_inside = false;
        } else if (!last_inside) {
            first = p;
            last_inside = true;
        } else {
            if (first) {
                void* memory = RailAuthoring::new_rail_memory(sizeof(EditedRail));
                auto* rail = memory ? memory::construct_in<EditedRail>(memory) : nullptr;
                rail->following_ = list;
                list = rail;
                rail->add_point_copy(first);
                first = nullptr;
            }
            list->add_point_copy(p);
            last_inside = true;
        }
    }
    return list;
}

// --- RailAuthoring -----------------------------------------------------------------------------------------

// 0x004F4970
RailAuthoring* RailAuthoring::sole()
{
    auto* object = core::down_cast<objects::GameEntity>(objects::ObjectHubBase::look_up_entity(kType));
    return core::down_cast<RailAuthoring>(object->fetch_module(kType));
}

// 0x004F7FB0
gsl::owner<objects::ObjectModule*> RailAuthoring::fabricate() { return new RailAuthoring; }

// 0x004F7C90
RailAuthoring::RailAuthoring()
{
    set_kind_tag(kType);
    make_point_pool(kMaxPoints, "CEditedRailPoint");
    make_rail_pool(kMaxRails, "CEditedRail");
    packed_ = {};
    get_clear();
}

// 0x004F7D20 (0x004F8010 the deleting one)
RailAuthoring::~RailAuthoring()
{
    get_clear();
    free_pool(point_pools());
    free_pool(park_editing_state().rail_pools());
}

// 0x004F47E0
void RailAuthoring::seal()
{
    input_ptr_ = entity()->fetch_module(kInput);
    view_camera_ = entity()->fetch_module(kEditorCamera);
}

// 0x004F4990
void RailAuthoring::make_point_pool(int count, const char* name)
{
    make_pool(point_pools(), sizeof(EditedRailPoint), count, name);
}

// 0x004F4A70
void RailAuthoring::make_rail_pool(int count, const char* name)
{
    make_pool(park_editing_state().rail_pools(), sizeof(EditedRail), count, name);
}

// 0x004F4AF0
void* RailAuthoring::new_rail_memory(unsigned int size) { return park_editing_state().rail_pools().carve(size); }

// 0x004F4B30
void RailAuthoring::free_rail(void* rail) { park_editing_state().rail_pools().dealloc(rail); }

// 0x004F4A10
void RailAuthoring::free_point(void* point) { point_pools().dealloc(point); }

// 0x004F6BF0
int RailAuthoring::free_points()
{
    const memory::PoolPair& pools = point_pools();
    return kMaxPoints - core::at(pools.pools(), pools.current())->taken_count();
}

// 0x004F7550
void RailAuthoring::get_clear()
{
    EditedRail* rail = rail_groups_;
    while (rail) {
        EditedRail* next = rail->following();
        rail->get_clear();
        free_rail(rail);
        rail = next;
    }
    rail_groups_ = nullptr;
    active_node_ = nullptr;
    mode_sel_ = 0;
    delete_dotted_line();
}

// 0x004F72C0
EditedRail* RailAuthoring::get_new_rail()
{
    void* memory = new_rail_memory(sizeof(EditedRail));
    EditedRail* rail = memory ? memory::construct_in<EditedRail>(memory) : nullptr;
    // (out of rails: a crash here, as in the original)
    Expects(rail != nullptr);
    rail->set_following(rail_groups_);
    rail->set_preceding(nullptr);
    if (rail_groups_) {
        rail_groups_->set_preceding(rail);
    }
    rail_groups_ = rail;
    active_node_ = rail;
    return rail;
}

// 0x004F7270
void RailAuthoring::delete_rail(EditedRail* rail)
{
    uncouple(this, rail);
    rail->get_clear();
    free_rail(rail);
}

// 0x004F7370
void RailAuthoring::delete_short_rails()
{
    EditedRail* rail = rail_groups_;
    while (rail) {
        EditedRail* next = rail->following();
        if (!rail->point_chain() || !rail->point_chain()->following()) {
            uncouple(this, rail);
            rail->get_clear();
            free_rail(rail);
        }
        rail = next;
    }
}

// 0x004F73E0
bool RailAuthoring::cut_rail(EditedRail* rail, EditedRailPoint* point)
{
    EditedRailPoint* rest = rail->cut_at(point);
    if (!rest) {
        return false;
    }
    get_new_rail()->set_point_chain(rest);
    return true;
}

// 0x004F7410
bool RailAuthoring::cut_point_in_or_out(EditedRail* rail, float x0, float z0, float x1, float z1, int inside)
{
    EditedRailPoint* p = rail->point_chain();
    while (p) {
        EditedRailPoint* next = p->following();
        const bool cut = get_inside(p, x0, z0, x1, z1) ? inside == 1 : inside == 0;
        if (cut) {
            EditedRailPoint* rest = rail->cut_at(p);
            if (rest) {
                get_new_rail()->set_point_chain(rest);
                return true;
            }
        }
        p = next;
    }
    return false;
}

// 0x004F74B0
void RailAuthoring::delete_points(float x0, float z0, float x1, float z1, int inside)
{
    if (x0 > x1) {
        const float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (z0 > z1) {
        const float t = z0;
        z0 = z1;
        z1 = t;
    }
    bool changed = false;
    for (bool again = true; again; again = changed) {
        changed = false;
        for (EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
            if (cut_point_in_or_out(rail, x0, z0, x1, z1, inside)) {
                changed = true;
            }
        }
    }
    delete_short_rails();
}

// 0x004F3C50
EditedRail* RailAuthoring::find_rail(uint32_t id) const
{
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        if (rail->ident() == id) {
            return rail;
        }
    }
    return nullptr;
}

// 0x004F3CA0
EditedRail* RailAuthoring::find_rail_by_cluster(uint32_t name) const
{
    int i = 0;
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following(), ++i) {
        std::array<char, kClusterNameSize> cluster{};
        text::format_to(cluster, "CreatedRailCluster%d", i);
        if (name == checksums::hash_from_text(cluster.data())) {
            return rail;
        }
    }
    return nullptr;
}

// 0x004F3D10
void RailAuthoring::clear_cluster_colours(uint32_t name) const
{
    if (EditedRail* rail = find_rail_by_cluster(name)) {
        rail->clear_colours();
    }
}

// 0x004F4950
void RailAuthoring::set_cluster_colour(checksums::Name cluster, int colour_index) const
{
    const uint32_t name = cluster.value();
    if (EditedRail* rail = find_rail_by_cluster(name)) {
        rail->change_colour(colour_index);
    }
}

// 0x004F3C70
int RailAuthoring::rail_point_total() const
{
    int count = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        for (const EditedRailPoint* p = rail->point_chain(); p; p = p->following()) {
            ++count;
        }
    }
    return count;
}

// 0x004F3C20
void RailAuthoring::un_highlight_all()
{
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        for (EditedRailPoint* p = rail->point_chain(); p; p = p->following()) {
            p->clear_glow();
        }
    }
}

// 0x004F3D30
void RailAuthoring::change_live(bool active)
{
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        for (const EditedRailPoint* p = rail->point_chain(); p; p = p->following()) {
            if (p->post()) {
                enable_sector(p->post(), active);
            }
            if (p->segment()) {
                enable_sector(p->segment(), active);
            }
        }
    }
}

// 0x004F45C0
void RailAuthoring::delete_all_segments()
{
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        rail->delete_segments();
    }
}

// 0x004F45E0
void RailAuthoring::delete_all_posts()
{
    for (EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        rail->delete_posts();
    }
}

// 0x004F4600
bool RailAuthoring::any_point_outside(float x0, float z0, float x1, float z1) const
{
    if (x0 > x1) {
        const float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (z0 > z1) {
        const float t = z0;
        z0 = z1;
        z1 = t;
    }
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        if (rail->any_point_outside(x0, z0, x1, z1)) {
            return true;
        }
    }
    return false;
}

// 0x004F4690
int RailAuthoring::count_points_inside(float x0, float z0, float x1, float z1) const
{
    if (x0 > x1) {
        const float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (z0 > z1) {
        const float t = z0;
        z0 = z1;
        z1 = t;
    }
    int count = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        count += rail->count_points_inside(x0, z0, x1, z1);
    }
    return count;
}

// 0x004F4710
bool RailAuthoring::find_nearest_point(const maths::Vec4* position, maths::Vec4* point, float* distance_squared,
                                       uint32_t* rail_id, int* index, uint32_t skip_rail, int bypass_idx) const
{
    bool found = false;
    float best = kFarthestDistanceSquared;
    // The original keeps the callee's results in locals it never initialises: a rail whose only point is skipped
    // still "finds" one - the distance left from an earlier rail (or, for the first, the bits of the `index` pointer
    // the slot held), the point unset (here zero: what a cleared stack holds).
    maths::Vec4 nearest = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
    float distance = 0.0F;
    std::memcpy(&distance, static_cast<const void*>(&index), sizeof(distance));
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        int i = 0;
        if (rail->find_nearest_point(position, &nearest, &distance, &i, rail->ident() == skip_rail ? bypass_idx : -1) &&
            distance < best) {
            best = distance;
            *point = nearest;
            *distance_squared = distance;
            *rail_id = rail->ident();
            *index = i;
            found = true;
        }
    }
    return found;
}

// 0x004F48F0
void RailAuthoring::delete_dotted_line()
{
    if (!dotted_line_) {
        return;
    }
    remove_scene_sector(dotted_line_);
    dotted_line_name_ = 0;
}

// 0x004F4500
void RailAuthoring::get_pack()
{
    // (packed as a record, then written as the buffer's bytes - rail_buffer_format.h)
    RailBuffer packed{};
    uint16_t rails = 0;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        ++rails;
    }
    packed.rail_count_ = rails;
    const EditedRail* rail = rail_groups_;
    if (rail) {
        while (rail->following()) {
            rail = rail->following();
        }
        int count = 0;
        int record = 0;
        // From the first rail laid (the tail) back to the head, each rail's points from its first laid.
        for (; rail; rail = rail->preceding()) {
            uint16_t points = 0;
            for (const EditedRailPoint* p = rail->point_chain(); p; p = p->following()) {
                ++points;
            }
            core::at(packed.point_counts_, count++) = points;
            const EditedRailPoint* p = rail->point_chain();
            if (!p) {
                continue;
            }
            while (p->following()) {
                p = p->following();
            }
            for (; p; p = p->preceding()) {
                p->get_pack(core::at(packed.records_, record++));
            }
        }
    }
    util::ByteWriter out(packed_);
    rail_buffer_format::write_rail_buffer(out, packed);
}

// 0x004F45B0
uint8_t* RailAuthoring::get_packed()
{
    get_pack();
    return packed_.data();
}

// 0x004F3C00
void RailAuthoring::set_packed(const void* data, size_t size)
{
    // Not original - read through the format's reader: the original copied 0x1132 bytes whatever the message's size
    // (reading past a short one), and unpack walked the counts as they came (past the point counts and records for
    // more than 200 rails or 400 points).
    util::ByteReader reader(data, size);
    RailBuffer rails{};
    if (!rail_buffer_format::read_rail_buffer(reader, &rails)) {
        debug::printf("rail data damaged: cut short or more rails / points than the editor holds\n");
    }
    util::ByteWriter out(packed_);
    rail_buffer_format::write_rail_buffer(out, rails); // (no rails when damaged)
}

// 0x004F7DF0
void RailAuthoring::unpack()
{
    get_clear();
    // (the buffer's bytes decoded; Not original - counts past the arrays: no rails)
    RailBuffer packed{};
    util::ByteReader in(packed_);
    if (!rail_buffer_format::read_rail_buffer(in, &packed)) {
        return;
    }
    int record_index = 0;
    for (int index = 0; std::cmp_less(index, packed.rail_count_); ++index) {
        const uint16_t points = core::at(packed.point_counts_, index);
        EditedRail* rail = get_new_rail();
        for (unsigned int i = points; i != 0; --i) {
            const std::array<uint16_t, kPackedPointWords>& record = core::at(packed.records_, record_index++);
            if (EditedRailPoint* p = rail->append_rail_vertex()) {
                p->set_has_post(core::at(record, 4) != 0);
                p->set_level_y(static_cast<float>(static_cast<double>(core::at(record, 3)) * kHeightUnpackScale));
                p->at_point().x = static_cast<float>(static_cast<double>(static_cast<int16_t>(core::at(record, 0))) * kPositionUnpackScale);
                p->at_point().y = static_cast<float>(static_cast<double>(static_cast<int16_t>(core::at(record, 1))) * kPositionUnpackScale);
                p->at_point().z = static_cast<float>(static_cast<double>(static_cast<int16_t>(core::at(record, 2))) * kPositionUnpackScale);
            }
        }
    }
}

// 0x004F7230
EditedRail* RailAuthoring::copy_rails_inside(float x0, float z0, float x1, float z1) const
{
    EditedRail* list = nullptr;
    for (const EditedRail* rail = rail_groups_; rail; rail = rail->following()) {
        list = rail->copy_inside(x0, z0, x1, z1, list);
    }
    return list;
}

} // namespace park_editing
