// The park editor's clipboards (0x0058B7E0-0x0058C984): a copied area - the metapieces in it (CClipboardEntry: the
// palette piece, its cell relative to the park, its turn and size) and its rails (the rail editor's) - shown at the
// cursor, turned with it, and pasted. See park.h.

#include <cmath>
#include <cstdint>

#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"
#include "game/park_editing/rail_editor.h"

namespace park_editing {

maths::Vec4 g_rail_origin;
memory::PoolPair g_rail_pools;
memory::PoolPair g_clipboard_entry_pools;

namespace {

constexpr uint32_t kRiserBelow = 0x4A0D22DB; // riser_below
constexpr uint32_t kRiserFloor = 0xB0A98147; // riser_floor
constexpr int kNoHeight = -1000;
constexpr float kQuarterTurnDegrees = 90.0f;      // 0x00645EB0
constexpr float kRadiansPerDegree = 0.017453292f; // 0x00645464

// The clipboard entries' pool pair (CPoolable, 0x18-byte items).
memory::PoolPair& entry_pools() { return g_clipboard_entry_pools; }

// The rail editor (0x004F4970) and its rails (CEditedRail objects linked by their first word).
void* get_rail_editor() { return RailAuthoring::sole(); }
// 0x004F7230: copies of the rails in the world box (x0, z0)-(x1, z1); 0x004F6BF0: how many rail points are free;
// 0x004F72C0: a new rail.
void* copy_rails_in_box(void* editor, float x0, float z0, float x1, float z1)
{
    return static_cast<RailAuthoring*>(editor)->copy_rails_inside(x0, z0, x1, z1);
}
int free_rail_points(void* editor)
{
    (void)editor;
    return RailAuthoring::free_points();
}
void* get_new_rail(void* editor) { return static_cast<RailAuthoring*>(editor)->get_new_rail(); }
void* follow_rail_link(void* rail) { return *static_cast<void**>(rail); }
// A rail's points (0x004F3B90); a copy of `from` (0x004F6F40); placed at `corner` and `cursor` turned by `angle`
// (0x004F4370); two transforms with an origin, a position and an angle (0x004F61C0, 0x004F6210); hidden (0x004F4220,
// 0x004F41C0); its destructor body (0x004F70F0); its pool pair (0x007CCD40).
int rail_point_count(void* rail) { return static_cast<EditedRail*>(rail)->rail_point_total(); }
void duplicate_rail_path(void* rail, void* from)
{
    static_cast<EditedRail*>(rail)->copy_from(static_cast<const EditedRail*>(from));
}
void place_rail(void* rail, maths::Vec4* corner, maths::Vec4* cursor, float angle)
{
    static_cast<EditedRail*>(rail)->xform(corner, cursor, angle);
}
void transform_rail1(void* rail, const maths::Vec4* origin, const maths::Vec4* position, float angle)
{
    static_cast<EditedRail*>(rail)->draw_segments(origin, position, angle);
}
void transform_rail2(void* rail, const maths::Vec4* origin, const maths::Vec4* position, float angle)
{
    static_cast<EditedRail*>(rail)->draw_posts(origin, position, angle);
}
void hide_rail(void* rail)
{
    static_cast<EditedRail*>(rail)->delete_posts();
    static_cast<EditedRail*>(rail)->delete_segments();
}
void destroy_rail(void* rail) { static_cast<EditedRail*>(rail)->get_clear(); }
memory::PoolPair& rail_pools() { return g_rail_pools; }
const maths::Vec4* rail_origin() { return &g_rail_origin; } // (zero)

// A preview metapiece taken out of the park.
void destroy_preview(ClipboardEntry* entry)
{
    entry->preview_->flag_bits_ &= ~PieceGroup::kCursor;
    ParkLayout::singleton()->free_placed_piece(entry->preview_, 0);
    ParkLayout::singleton()->rebuild_park_geometry(0);
    entry->preview_ = nullptr;
}

} // namespace

// --- CClipboardEntry ---

// 0x0058B800
void ClipboardEntry::fetch_extent(CellBox* area, uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor)
{
    CellBox size = CellBox::get_make(0, 0, 0, 0, 0, 0);
    uint8_t width = 1, length = 1;
    if (piece_group_) {
        size = piece_group_->cell_footprint_;
        width = size.w;
        length = size.l;
    }
    if (((cursor->quarter_turns_ + quarter_turns_) & 3) & 1) {
        area->w = length;
        area->l = width;
    } else {
        area->w = width;
        area->l = length;
    }
    area->h = 1;
    // Its offset from the copy's centre, turned with the cursor (a turned piece's corner moves to its other side).
    int dx = pos_x_ - centre_x;
    int dz = depth_cell_ - centre_z;
    switch (cursor->quarter_turns_) {
    case 1: {
        const int x = dz;
        dz = -dx;
        dx = x;
        break;
    }
    case 2:
        dx = -dx;
        dz = -dz;
        break;
    case 3: {
        const int x = -dz;
        dz = dx;
        dx = x;
        break;
    }
    default:
        break;
    }
    switch (cursor->quarter_turns_) {
    case 1:
        dz += 1 - w_;
        break;
    case 2:
        dx += 1 - w_;
        dz += 1 - l_;
        break;
    case 3:
        dx += 1 - l_;
        break;
    default:
        break;
    }
    area->x = static_cast<uint8_t>(dx + cursor->cell_footprint_.x);
    area->z = static_cast<uint8_t>(dz + cursor->cell_footprint_.z);
    area->y = y_;
}

// 0x0058B940
bool ClipboardEntry::can_place(uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor)
{
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    fetch_extent(&area, centre_x, centre_z, cursor);
    area.y = 0;
    return ParkLayout::singleton()->can_change_floor_here(area, true);
}

// 0x0058B9C0
void ClipboardEntry::graft(uint8_t centre_x, int height, uint8_t centre_z, BuildPointer* cursor)
{
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    fetch_extent(&area, centre_x, centre_z, cursor);
    int top = y_ + height;
    if (piece_group_ && (piece_group_->flag_bits_ & PieceGroup::kBuiltShell))
        top += piece_group_->cell_footprint_.h;
    if (top >= 16)
        top = 15;
    area.y = static_cast<int8_t>(top);
    ParkLayout* manager = ParkLayout::singleton();
    // The floor raised to it (at most 41 steps; a flat floor already there stops it).
    bool flat = false;
    int floor = manager->get_ground_level(area, &flat);
    for (int steps = 0; floor <= top && steps <= 40; ++steps) {
        if (flat && floor == top)
            break;
        manager->raise_or_lower_floor(area, 1);
        flat = false;
        floor = manager->get_ground_level(area, &flat);
    }
    if (piece_group_ && !(piece_group_->flag_bits_ & PieceGroup::kBuiltShell)) {
        PieceGroup* meta = manager->make_placed_piece(piece_group_, 0);
        meta->set_quarter_turns((cursor->quarter_turns_ + quarter_turns_) & 3);
        manager->place_meta_piece_in_park(area, meta);
    }
}

// 0x0058BB20
int ClipboardEntry::fetch_tallness(uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor)
{
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    fetch_extent(&area, centre_x, centre_z, cursor);
    bool flat = false;
    return ParkLayout::singleton()->get_ground_level(area, &flat);
}

// 0x0058BBA0
void ClipboardEntry::glow_overlapping_pieces(uint8_t centre_x, uint8_t centre_z, BuildPointer* cursor, int on)
{
    CellBox area = CellBox::get_make(0, 0, 0, 0, 0, 0);
    fetch_extent(&area, centre_x, centre_z, cursor);
    area.y = 0;
    const MapListHolder metas(ParkLayout::singleton()->collect_pieces_in_region(area));
    for (const MapListNode* node = metas.list; node; node = node->following_) {
        PieceGroup* meta = node->get_piece_group()->get_cast();
        if (!(meta->flag_bits_ & PieceGroup::kBuiltShell))
            meta->spotlight(on, 0);
    }
}

// 0x0058BCA0
void ClipboardEntry::show_preview(uint8_t centre_x, int /*height*/, uint8_t centre_z, maths::Vec4 position, float angle)
{
    if (!piece_group_)
        return;
    // The middle of its cells from the copy's centre (in the world's units), turned by `angle`.
    const int dx = pos_x_ - centre_x;
    const int dz = depth_cell_ - centre_z;
    const auto x = static_cast<float>(((w_ - 1) * 0.5 + dx) * kGridPitch);
    const double z = ((l_ - 1) * 0.5 + dz) * kGridStepAlong;
    const double radians = static_cast<double>(angle) * kRadiansPerDegree;
    const auto sine = static_cast<float>(std::sin(radians));
    const double cosine = std::cos(radians);
    const auto turned_x = static_cast<float>(z * sine + x * cosine);
    const double turned_z = z * cosine - static_cast<double>(x) * sine;
    // (The original adds an offset whose w it never sets - stack garbage - to the position's: the position's w kept.)
    maths::Vec4 at;
    at.w = position.w;
    at.z = static_cast<float>(position.z + turned_z);
    at.x = position.x + turned_x;
    at.y = static_cast<float>(y_ * static_cast<double>(kGridStepUp) + position.y);
    if (!preview_) {
        preview_ = ParkLayout::singleton()->make_placed_piece(piece_group_, 0x204);
        preview_->flag_bits_ |= PieceGroup::kCursor;
    }
    preview_->change_place(at, static_cast<float>(quarter_turns_ * static_cast<double>(kQuarterTurnDegrees) + angle));
}

// --- CClipboard ---

// 0x0058B7E0
bool Clipboard::is_vacant() { return metas_ ? false : rail_groups_ == nullptr; }

// 0x0058BDF0
Clipboard::Clipboard()
{
    edit_span_.assign_value(0, 0, 0, 0, 0, 0);
    edit_span_.assign_value(0, 0, 0, 0, 0, 0);
}

// 0x0058BE30
int Clipboard::fetch_tallness(BuildPointer* cursor)
{
    const auto centre_x = static_cast<uint8_t>((lo_cell_x_ + hi_cell_x_) / 2);
    const auto centre_z = static_cast<uint8_t>((lo_cell_z_ + hi_cell_z_) / 2);
    int highest = kNoHeight;
    for (ClipboardEntry* entry = metas_; entry; entry = entry->following_) {
        const int height = entry->fetch_tallness(centre_x, centre_z, cursor);
        if (height > highest)
            highest = height;
    }
    return highest == kNoHeight ? 0 : highest;
}

// 0x0058BEB0
void Clipboard::glow_overlapping_pieces(BuildPointer* cursor, int on)
{
    const auto centre_x = static_cast<uint8_t>((lo_cell_x_ + hi_cell_x_) / 2);
    const auto centre_z = static_cast<uint8_t>((lo_cell_z_ + hi_cell_z_) / 2);
    for (ClipboardEntry* entry = metas_; entry; entry = entry->following_)
        entry->glow_overlapping_pieces(centre_x, centre_z, cursor, on);
}

// 0x0058BF10
void Clipboard::fetch_extent(BuildPointer* cursor, CellBox* area)
{
    const int left = lo_cell_x_ - static_cast<uint8_t>((lo_cell_x_ + hi_cell_x_) / 2);
    const int near = lo_cell_z_ - static_cast<uint8_t>((lo_cell_z_ + hi_cell_z_) / 2);
    int dx = 0, dz = 0;
    switch (cursor->quarter_turns_) {
    case 0:
        area->l = edit_span_.l;
        area->w = edit_span_.w;
        area->h = 1;
        dx = left;
        dz = near;
        break;
    case 1:
        area->w = edit_span_.l;
        area->l = edit_span_.w;
        area->h = 1;
        dx = near;
        dz = 1 - edit_span_.w - left;
        break;
    case 2:
        area->w = edit_span_.w;
        area->l = edit_span_.l;
        area->h = 1;
        dx = 1 - edit_span_.w - left;
        dz = 1 - edit_span_.l - near;
        break;
    case 3:
        area->w = edit_span_.l;
        area->l = edit_span_.w;
        area->h = 1;
        dx = 1 - edit_span_.l - near;
        dz = left;
        break;
    default:
        break;
    }
    area->z = static_cast<uint8_t>(dz + cursor->cell_footprint_.z);
    area->x = static_cast<uint8_t>(dx + cursor->cell_footprint_.x);
    area->y = 0;
}

// 0x0058C050
maths::Vec4* Clipboard::get_centre(maths::Vec4* centre)
{
    // The cell in the middle of the copy (one high, at the copy's floor), and half its size on.
    const auto centre_x = static_cast<uint8_t>((lo_cell_x_ + hi_cell_x_) / 2);
    const auto centre_z = static_cast<uint8_t>((lo_cell_z_ + hi_cell_z_) / 2);
    CellBox cell = CellBox::get_make(0, 0, 0, 0, 0, 0);
    cell.y = edit_span_.y;
    cell.w = 1;
    cell.h = 1;
    cell.l = 1;
    cell.z = centre_z;
    cell.x = centre_x;
    maths::Vec4 size;
    maths::Vec4 corner;
    const maths::Vec4* world = ParkLayout::singleton()->grid_dims_to_world(&corner, &cell, &size);
    const maths::Vec4 at = *world;
    centre->x = static_cast<float>(size.x * 0.5 + at.x);
    centre->y = at.y;
    centre->z = static_cast<float>(size.z * 0.5 + at.z);
    centre->w = at.w;
    return centre;
}

// 0x0058C130
void Clipboard::preview_pieces(maths::Vec4 position, float angle, int height)
{
    const auto centre_x = static_cast<uint8_t>((lo_cell_x_ + hi_cell_x_) / 2);
    const auto centre_z = static_cast<uint8_t>((lo_cell_z_ + hi_cell_z_) / 2);
    for (ClipboardEntry* entry = metas_; entry; entry = entry->following_)
        entry->show_preview(centre_x, height, centre_z, position, angle);
    maths::Vec4 centre;
    get_centre(&centre);
    for (void* rail = rail_groups_; rail; rail = follow_rail_link(rail)) {
        transform_rail1(rail, &centre, &position, angle);
        transform_rail2(rail, &centre, &position, angle);
    }
}

// 0x0058C200
void Clipboard::free_all_piece_groups()
{
    for (ClipboardEntry* entry = metas_; entry; entry = entry->following_) {
        if (entry->preview_)
            destroy_preview(entry);
    }
    for (void* rail = rail_groups_; rail; rail = follow_rail_link(rail))
        hide_rail(rail);
}

// 0x0058C270
bool Clipboard::add_row(PieceGroup* meta, int dy)
{
    const CellBox area = meta->cell_footprint_;
    const auto y = static_cast<int8_t>(area.y + dy);
    PieceGroup* source = ParkLayout::singleton()->fetch_meta_template(meta->name_key_, nullptr);
    // (Already in it: done.)
    for (const ClipboardEntry* entry = metas_; entry; entry = entry->following_) {
        if (entry->piece_group_ == source && entry->pos_x_ == area.x && entry->y_ == y &&
            entry->depth_cell_ == area.z && entry->w_ == area.w && entry->l_ == area.l &&
            entry->quarter_turns_ == meta->quarter_turns_)
            return true;
    }
    memory::PoolPair& pools = entry_pools();
    const memory::SlabBank* pool = pools.pools[pools.current]; // (not null-checked: as the original)
    if (pool->taken_count_ == pool->num_)
        return false;
    auto* entry = pools.allocate<ClipboardEntry>();
    if (entry) {
        entry->piece_group_ = nullptr;
        entry->preview_ = nullptr;
        entry->depth_cell_ = 0;
        entry->y_ = 0;
        entry->pos_x_ = 0;
        entry->w_ = 0;
        entry->l_ = 0;
        entry->quarter_turns_ = 0;
        entry->following_ = nullptr;
    }
    entry->y_ = y; // (not null-checked: as the original)
    entry->depth_cell_ = area.z;
    entry->piece_group_ = source;
    entry->pos_x_ = area.x;
    entry->w_ = area.w;
    entry->l_ = area.l;
    // The copy's bounds take in both corners.
    auto take_in = [this](uint8_t x, uint8_t z) {
        if (x < lo_cell_x_)
            lo_cell_x_ = x;
        if (x > hi_cell_x_)
            hi_cell_x_ = x;
        if (z < lo_cell_z_)
            lo_cell_z_ = z;
        if (z > hi_cell_z_)
            hi_cell_z_ = z;
    };
    take_in(area.x, area.z);
    take_in(static_cast<uint8_t>(area.w + area.x - 1), static_cast<uint8_t>(area.l + area.z - 1));
    entry->quarter_turns_ = meta->quarter_turns_;
    entry->following_ = metas_;
    metas_ = entry;
    return true;
}

// 0x0058C430 (also reached through 0x0058C980)
void Clipboard::teardown()
{
    memory::PoolPair& pools = entry_pools();
    for (ClipboardEntry* entry = metas_; entry;) {
        ClipboardEntry* next = entry->following_;
        if (entry->preview_)
            destroy_preview(entry);
        pools.dealloc(entry);
        entry = next;
    }
    metas_ = nullptr;
    lo_cell_z_ = 0xFF;
    lo_cell_x_ = 0xFF;
    hi_cell_z_ = 0;
    hi_cell_x_ = 0;
    edit_span_.assign_value(0, 0, 0, 0, 0, 0);
    for (void* rail = rail_groups_; rail;) {
        void* next = follow_rail_link(rail);
        destroy_rail(rail);
        rail_pools().dealloc(rail);
        rail = next;
    }
    rail_groups_ = nullptr;
}

// 0x0058C590
bool Clipboard::copy_picked_to_clipboard(CellBox area)
{
    edit_span_ = area;
    ParkLayout* manager = ParkLayout::singleton();
    // The lowest piece in the area (not above the floor: 0 at most) - risers below count 15 higher, floor risers 1.
    int lowest = 0;
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            for (const MapListNode* node = manager->get_cell_chain(x, z); node; node = node->following_) {
                const PieceGroup* meta = node->get_piece_group()->get_cast();
                int y = meta->cell_footprint_.y;
                if (meta->name_key_ == kRiserBelow)
                    y += 15;
                else if (meta->name_key_ == kRiserFloor)
                    y += 1;
                if (y < lowest)
                    lowest = y;
            }
        }
    }
    const int dy = -lowest;
    // Every metapiece wholly inside (not restart points or risers below), raised by that.
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z) {
            for (const MapListNode* node = manager->get_cell_chain(x, z); node; node = node->following_) {
                PieceGroup* meta = node->get_piece_group()->get_cast();
                if (meta->is_spawn_marker() || !meta->fits_inside_region(&area) || meta->name_key_ == kRiserBelow)
                    continue;
                if (!add_row(meta, dy)) {
                    teardown();
                    return false;
                }
            }
        }
    }
    // The rails over it.
    maths::Vec4 size;
    maths::Vec4 corner;
    const maths::Vec4* world = manager->grid_dims_to_world(&corner, &area, &size);
    const float x0 = world->x;
    const float z0 = world->z;
    rail_groups_ = copy_rails_in_box(get_rail_editor(), x0, z0, size.x + x0, size.z + z0);
    return true;
}

// 0x0058C780
bool Clipboard::show_at(BuildPointer* cursor)
{
    ParkBuilderMode* editor = ParkBuilderMode::fabricate(false);
    if (!editor->main_heap_has_room(hi_cell_x_ - lo_cell_x_ + 1, hi_cell_z_ - lo_cell_z_ + 1)) {
        ParkBuilderMode::get_release();
        return false;
    }
    const auto centre_x = static_cast<uint8_t>((lo_cell_x_ + hi_cell_x_) / 2);
    const auto centre_z = static_cast<uint8_t>((lo_cell_z_ + hi_cell_z_) / 2);
    for (ClipboardEntry* entry = metas_; entry; entry = entry->following_) {
        if (!entry->can_place(centre_x, centre_z, cursor)) {
            ParkBuilderMode::get_release();
            return false;
        }
    }
    int rail_points = 0;
    for (void* rail = rail_groups_; rail; rail = follow_rail_link(rail))
        rail_points += rail_point_count(rail);
    if (rail_points > free_rail_points(get_rail_editor())) {
        ParkBuilderMode::get_release();
        return false;
    }
    for (ClipboardEntry* entry = metas_; entry; entry = entry->following_)
        entry->graft(centre_x, cursor->clipboard_height_, centre_z, cursor);
    maths::Vec4 centre;
    get_centre(&centre);
    maths::Vec4 at = *editor->get_cursor_pos();
    for (void* rail = rail_groups_; rail; rail = follow_rail_link(rail)) {
        void* copy = get_new_rail(get_rail_editor());
        duplicate_rail_path(copy, rail);
        place_rail(copy, &centre, &at,
                   static_cast<float>(cursor->quarter_turns_ * static_cast<double>(kQuarterTurnDegrees)));
        transform_rail1(copy, rail_origin(), rail_origin(), 0.0f);
        transform_rail2(copy, rail_origin(), rail_origin(), 0.0f);
    }
    ParkBuilderMode::get_release();
    return true;
}

} // namespace park_editing
