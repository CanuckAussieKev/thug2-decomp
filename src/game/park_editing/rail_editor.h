#pragma once

// The park editor's rail editor (0x004F3AD0-0x004F8030): the rails the player lays down point by point in a created
// park. The rail editor component (type 0x5B509AD3, on the object of the same id; RailEditor() finds it) keeps a list of
// rails (CEditedRail, from a pool of 200), each a list of points (CEditedRailPoint, from a pool of 400): a position, an
// optional post standing on the ground below, and the two dynamic sectors that show them - the rail's piece from the
// point to the next (cloned from Sk5Ed_RA_Dynamic, stretched and turned to fit) and the post (Sk5Ed_RAp_Dynamic). The
// rails are kept in the park file and in network messages as a packed buffer (the component's +0x34), in a structure
// (CreatedRails: an array of {points: [{pos, HasPost}]}), and become rail_nodes of the park's node array
// (ncomp_railnode_park, clusters named CreatedRailCluster<n>).
//
// Pools (CPoolable pairs): the points' at 0x007CCD50 ("CEditedRailPoint", 0x28-byte items), the rails' at 0x007CCD40
// ("CEditedRail", 0x10-byte items) - made by the component's constructor, freed by its destructor.

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "formats/rail_buffer_format.h"
#include <gsl/pointers>
#include <array>
#include <span>

namespace render {
class WorldCell;
}
namespace script {
class Array;
class Script;
class Struct;
} // namespace script

namespace park_editing {

// A point of an edited rail (0x28 bytes).
struct EditedRailPoint {
    // The fields other code reads and sets.
    maths::Vec4& at_point() { return at_point_; }
    [[nodiscard]] const maths::Vec4& at_point() const { return at_point_; }
    [[nodiscard]] bool has_post() const { return has_post_; }
    void set_has_post(bool value) { has_post_ = value; }
    void set_highlighted(bool value) { highlighted_ = value; }
    [[nodiscard]] float level_y() const { return level_y_; }
    void set_level_y(float value) { level_y_ = value; }
    [[nodiscard]] EditedRailPoint* following() const { return following_; }
    void set_following(EditedRailPoint* value) { following_ = value; }
    [[nodiscard]] EditedRailPoint* preceding() const { return preceding_; }
    void set_preceding(EditedRailPoint* value) { preceding_ = value; }
    render::WorldCell*& segment() { return segment_; }
    [[nodiscard]] render::WorldCell* segment() const { return segment_; }
    void set_segment(render::WorldCell* value) { segment_ = value; }
    render::WorldCell*& post() { return post_; }
    [[nodiscard]] render::WorldCell* post() const { return post_; }
    void set_post(render::WorldCell* value) { post_ = value; }

private:
    maths::Vec4 at_point_; // +0x00 (w = 1)
    bool has_post_;        // +0x10 a post stands under it
    bool highlighted_;     // +0x11 its sectors coloured (Highlight)
    std::array<uint8_t, 2> pad12;
    float level_y_;              // +0x14 how far over the ground below it is (where its post stands)
    EditedRailPoint* following_; // +0x18
    EditedRailPoint* preceding_; // +0x1C
    render::WorldCell* segment_;  // +0x20 the sector of the rail from here to the next point
    render::WorldCell* post_;     // +0x24 its post's sector

public:

    // 0x004F3AD0: if highlighted, its sectors (and those of its neighbours that Highlight coloured: the previous
    // point's segment, an end point's post) back to their own colour and shown in every viewport; not highlighted.
    void clear_glow();
    // 0x004F40A0 / 0x004F40F0: the segment's / the post's sector deleted from the park (and the collision rebuilt
    // unless rail sectors are being destroyed in bulk - 0x0067F000).
    void delete_segment();
    void delete_post();
    // 0x004F4140: written into a 10-byte network / park-file record: x, y, z * 8 (shorts), the height * 16 (0 when
    // negative), whether it has a post.
    void get_pack(std::array<uint16_t, 5>& record) const;
    // 0x004F5460: its segment drawn to the next point (none: deleted) - each point moved by `origin` -> `position`
    // turned `angle` degrees; Sk5Ed_RA_Dynamic cloned once. With a next point and a turn sharper than
    // RailEditorMaxJoinAngle the previous segment isn't joined on.
    void draw_segment(const maths::Vec4* origin, const maths::Vec4* position, float angle);
    // 0x004F56D0: its post drawn (with no post, or one shorter than the minimum: deleted) - Sk5Ed_RAp_Dynamic cloned
    // once, its vertices moved to stand from the ground to the point, turned along the rail.
    void draw_post(const maths::Vec4* origin, const maths::Vec4* position, float angle);
    // 0x004F5E30: its sectors coloured as RailEditorHighlightColour (or ...BadAngle... where a segment is too long /
    // too steep or a corner too sharp), flashing at RailEditorHighlightFlashRate with `flash`; the end posts too with
    // `includeEndPosts`. Once until clear_glow.
    void spotlight(bool flash, bool include_end_posts);
    // 0x004F5D10 (fastcall: ECX the position): the ground's height under a position (a feeler 2000 up and down;
    // nothing: 0).
    static float ground_height(const maths::Vec4* position);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(EditedRailPoint, has_post_) == 0x10 && offsetof(EditedRailPoint, highlighted_) == 0x11 &&
                          offsetof(EditedRailPoint, level_y_) == 0x14 && offsetof(EditedRailPoint, following_) == 0x18 &&
                          offsetof(EditedRailPoint, preceding_) == 0x1C && offsetof(EditedRailPoint, segment_) == 0x20 &&
                          offsetof(EditedRailPoint, post_) == 0x24 && sizeof(EditedRailPoint) == 0x28,
                      "layout must match the original");
    }
};

// A rail (0x10 bytes). Its points are kept newest first: the head is the last point laid.
struct EditedRail {
    // The fields other code reads and sets.
    [[nodiscard]] EditedRail* following() const { return following_; }
    void set_following(EditedRail* value) { following_ = value; }
    [[nodiscard]] EditedRail* preceding() const { return preceding_; }
    void set_preceding(EditedRail* value) { preceding_ = value; }
    [[nodiscard]] EditedRailPoint* point_chain() const { return point_chain_; }
    void set_point_chain(EditedRailPoint* value) { point_chain_ = value; }
    [[nodiscard]] uint32_t ident() const { return ident_; }

private:
    EditedRail* following_{nullptr};        // +0x00
    EditedRail* preceding_{nullptr};        // +0x04
    EditedRailPoint* point_chain_{nullptr}; // +0x08
    uint32_t ident_;               // +0x0C (from the counter 0x0067EFFC: unique)

public:

    // 0x004F70C0: the constructor: a new id, unlinked, no points.
    EditedRail();
    // 0x004F6E20 (also the destructor, 0x004F70F0 - a jump to it): every point's sectors deleted and the point freed.
    void get_clear();
    // 0x004F3B90
    [[nodiscard]] int rail_point_total() const;
    // 0x004F3BB0: the point `index` from the head (counting stops at the end: then null).
    [[nodiscard]] EditedRailPoint* fetch_vertex_at(int index) const;
    // 0x004F3BD0: its sectors' colours cleared.
    void clear_colours();
    // 0x004F41C0 / 0x004F4220: every point's segment / post deleted.
    void delete_segments();
    void delete_posts();
    // 0x004F4280: the point nearest `position` on the ground plane (x, z; the squared distance under 1e8), skipping
    // the point `skip`: its position, squared distance and index written. Whether the rail has any points.
    bool find_nearest_point(const maths::Vec4* position, maths::Vec4* point, float* distance_squared, int* index,
                            int skip) const;
    // 0x004F4310: how many points lie strictly inside (x0, z0)-(x1, z1).
    [[nodiscard]] int count_points_inside(float x0, float z0, float x1, float z1) const;
    // 0x004F4370: every point moved from `origin` to `position` turned `angle` degrees about y (w: the original adds
    // `position`'s to an unset local - here 0).
    void xform(const maths::Vec4* origin, const maths::Vec4* position, float angle);
    // 0x004F4420: its sectors coloured as graffitiColors[`index`].
    void change_colour(int index);
    // 0x004F44B0: whether any point lies outside [x0, x1] x [z0, z1].
    [[nodiscard]] bool any_point_outside(float x0, float z0, float x1, float z1) const;
    // 0x004F61C0 / 0x004F6210: every point's segment (from the last laid to the first) / post drawn.
    void draw_segments(const maths::Vec4* origin, const maths::Vec4* position, float angle);
    void draw_posts(const maths::Vec4* origin, const maths::Vec4* position, float angle);
    // 0x004F6240: a new point at the head (null when the pool is full - 400 used; an empty pool crashes, as in the
    // original). Its fields cleared, w = 1.
    EditedRailPoint* append_rail_vertex();
    // 0x004F62D0: a new point with `source`'s position, post and height.
    void add_point_copy(const EditedRailPoint* source);
    // 0x004F6300: `point` taken out: the rail cut there - the points after it (towards the head's end) returned as a
    // new list (null when there are none). A point inside the rail isn't taken out when no rail is free (returns
    // null). The head goes on its own.
    EditedRailPoint* cut_at(EditedRailPoint* point);
    // 0x004F63B0: the point `index` moved to `position` - unless a neighbour would be more than 4500 away - its height
    // over the ground kept, its post and segment drawn again (and the previous point's segment). Whether the move is
    // valid: both segments valid (rail_segment_valid) and the point inside the park (is_inside_park, 10 in). The flag
    // (UpdateRailPointPosition's UpdateSuperSectors) isn't read.
    bool move_point(int index, const maths::Vec4* position, bool update_super_sectors);
    // 0x004F6F40: `source`'s points copied (after clearing).
    void copy_from(const EditedRail* source);
    // 0x004F7100: copies of the runs of two or more points inside (x0, z0)-(x1, z1), each a new rail put before
    // `list` (linked by following_ only); returns the new list.
    EditedRail* copy_inside(float x0, float z0, float x1, float z1, EditedRail* list) const;
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(EditedRail, point_chain_) == 0x08 && offsetof(EditedRail, ident_) == 0x0C &&
                          sizeof(EditedRail) == 0x10,
                      "layout must match the original");
    }
};

// The rails packed for the park file and network messages: RailBuffer (formats/rail_buffer_format.h).

// The rail editor's geometry (rail_editor_draw.cpp).
// 0x004F3E00: a copy of the source sector `source` made in the park (counts dirty, collision rebuilt); its sector.
render::WorldCell* clone_rail_sector(uint32_t source);
// 0x004F3D80: `point` moved from `origin` to `position` turned `angle` degrees about y (x, z: the cosine at the x87's
// full precision, the sine rounded to float; w: the original adds `position`'s to an unset local - here 0). Returns
// `out`.
maths::Vec4* transform_rail_point(maths::Vec4* out, const maths::Vec4* point, const maths::Vec4* origin,
                                  const maths::Vec4* position, float angle);
// 0x004F3E40: whether the turn at `b` from `a` to `c` (on the ground plane) is gentle enough: the cosine between the
// two directions at least that of Rail_Corner_Leave_Angle.
bool rail_corner_ok(const maths::Vec4* a, const maths::Vec4* b, const maths::Vec4* c);
// 0x004F3F30 / 0x004F3F70: whether `a` and `b` are more than 4000 (a segment's longest) / 4500 (the dotted line's
// reach) apart. 1 or 0.
int rail_too_far(const maths::Vec4* a, const maths::Vec4* b);
int rail_out_of_reach(const maths::Vec4* a, const maths::Vec4* b);
// 0x004F3FB0: whether a segment from `a` to `b` is valid: at most 4000 long, at least
// RailEditorMinimumPointSeparation, no steeper than RailEditorMaxSlope.
bool rail_segment_valid(const maths::Vec4* a, const maths::Vec4* b);
// 0x004F4B90: the vertices `indices` picks turned about the first of them to face `direction` (its up kept
// up; straight up or down: facing z), then moved by `translation`.
void orient_rail_vertices(const maths::Vec4* direction, const maths::Vec4* translation, std::span<maths::Vec4> vertices,
                          std::span<const int> indices);
// 0x004F4E70: the source sector `source`'s vertices fitted between `from` and `to` (the start half turned towards
// the previous segment's direction and that segment's end moved onto it when `previous_sector` is given) and given to
// `sector`; with `update_collision`, its collision's vertices too.
void stretch_rail_sector(uint32_t source, const maths::Vec4* from, const maths::Vec4* previous, const maths::Vec4* to,
                         render::WorldCell* sector, render::WorldCell* previous_sector, bool update_collision);

// The rail editor component (type 0x5B509AD3, vtable 0x0064B918, 0x1168 bytes; made by 0x004F7FB0).
//
// Vtable: 0 Destroy, 1 Update, 3 Finalize, 8 Hide, 10 fetch_diagnostics, 12 invoke_method_by_name; the rest the base's /
// shared (2 / ... the empty 0x005BAB90, 4 / 9 the empty 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 the base's
// Suspend, 11 the base's member function table entry 0x0045D720).
class RailAuthoring final : public objects::ObjectModule {
public:
    RailAuthoring(const RailAuthoring&) = delete;
    RailAuthoring& operator=(const RailAuthoring&) = delete;
    static constexpr uint32_t kType = 0x5B509AD3;
    static constexpr int kMaxPoints = 400;
    static constexpr int kMaxRails = 200;

    // 0x004F4970: the rail editor (the component of the object 0x5B509AD3).
    static RailAuthoring* sole();
    // 0x004F7FB0 (registered by register_component_types): a new one (0 if out of memory).
    static gsl::owner<objects::ObjectModule*> fabricate();
    // 0x004F7C90: the pools made, no rails, the buffer cleared.
    RailAuthoring();
    // 0x004F7D20: the destructor body: cleared, the current pools freed (when made here).
    ~RailAuthoring() override;
    // 0x004F47E0 (vtable slot 3): the input and editor camera components.
    void seal() override;
    // 0x004F4810 (vtable slot 1): the pad's X / Triangle / Square / Circle presses run RailEditorX ... (each trigger
    // cleared), then RailEditorEveryFrame - on the object.
    void tick_frame() override;
    // 0x004F6F90 (vtable slot 8): hidden: rail_editor_destroy_cursor, the park's rails rebuilt, highlights and the
    // dotted line gone; shown: rail_editor_create_cursor, the park's rail nodes built, the camera told of the player
    // (its slot 2), the pad cleared.
    void conceal(bool hide) override;
    // 0x004F6C10 (vtable slot 10): NumFreeRailPoints, NumFreeRails, CurrentRail (its info or null) and Rails (an empty
    // structure each) added to `info`, then the base's (0x005BAB90).
    void fetch_diagnostics(script::Struct* info) override;
    // 0x004F75F0 (vtable slot 12): the script member functions: NewRail, AddNewPosition, DeleteRail, DeleteRailPoint,
    // UpdateRailPointPosition, HighlightRailPoint, UnHighlightAllRails, Get/SetEditingMode, GetCursorPosition,
    // GetEditedRailInfo, DrawDottedLine, DeleteDottedLine, ClearRailEditor, DestroyEditedRailSectors, and 0x31F66E6B
    // (no rail points free). 2 for other names.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;

    // 0x004F7550: every rail freed, no current rail, mode 0, the dotted line deleted.
    void get_clear();
    // 0x004F72C0: a new rail at the head of the list, made current (returns it; out of rails crashes).
    EditedRail* get_new_rail();
    // 0x004F7270: `rail` taken out of the list and freed.
    void delete_rail(EditedRail* rail);
    // 0x004F7370: the rails with fewer than two points freed.
    void delete_short_rails();
    // 0x004F73E0: `rail` cut at `point` (cut_at); the points after it made a new rail. Whether it was cut.
    bool cut_rail(EditedRail* rail, EditedRailPoint* point);
    // 0x004F7410: the first point of `rail` inside (x0, z0)-(x1, z1) (`inside` 1) or outside it (`inside` 0) that
    // can be cut out, cut out (the rest a new rail). Whether one was.
    bool cut_point_in_or_out(EditedRail* rail, float x0, float z0, float x1, float z1, int inside);
    // 0x004F74B0: every point inside / outside the rectangle (corners in any order) cut out, then the short rails
    // freed.
    void delete_points(float x0, float z0, float x1, float z1, int inside);
    // 0x004F3C50: the rail whose id is `id` (null if none).
    [[nodiscard]] EditedRail* find_rail(uint32_t id) const;
    // 0x004F3CA0: the rail the cluster `name` (CreatedRailCluster<n>: the n-th rail) stands for (null if none).
    [[nodiscard]] EditedRail* find_rail_by_cluster(uint32_t name) const;
    // 0x004F3D10 (thiscall, RET 4): that rail's colours cleared. 0x004F4950: that rail coloured as graffitiColors[i].
    void clear_cluster_colours(uint32_t name);
    void set_cluster_colour(uint32_t name, int colour_index);
    // 0x004F3C70: the points of every rail.
    [[nodiscard]] int rail_point_total() const;
    // 0x004F3C20: every point UnHighlighted.
    void un_highlight_all();
    // 0x004F3D30: every rail's sectors drawn and collided with, or neither.
    void change_live(bool active);
    // 0x004F45C0 / 0x004F45E0: every rail's segments / posts deleted.
    void delete_all_segments();
    void delete_all_posts();
    // 0x004F4600: whether any rail point lies outside the rectangle (corners in any order).
    [[nodiscard]] bool any_point_outside(float x0, float z0, float x1, float z1) const;
    // 0x004F4690: how many rail points lie inside it.
    [[nodiscard]] int count_points_inside(float x0, float z0, float x1, float z1) const;
    // 0x004F4710: the rail point nearest `position` (ground plane), skipping the point `bypass_idx` of the rail
    // `skip_rail`: its position, squared distance, rail id and index. Whether one was found.
    bool find_nearest_point(const maths::Vec4* position, maths::Vec4* point, float* distance_squared, uint32_t* rail_id,
                            int* index, uint32_t skip_rail, int bypass_idx) const;
    // 0x004F48F0: the dotted line's sector deleted.
    void delete_dotted_line();
    // 0x004F6AF0: the dotted line from the current rail's head point to `position` (green when that segment would be
    // valid, red - flashing at RailEditorRedLineFlashRate - when not); none when too far.
    void draw_dotted_line(const maths::Vec4* position);
    // 0x004F4500: the rails packed into the buffer. 0x004F45B0: that, and the buffer.
    void get_pack();
    uint8_t* get_packed();
    // 0x004F3C00: the buffer read from `size` bytes at `data` (rail_buffer_format.h; not original: a buffer cut short
    // or with counts past its arrays reads as no rails). 0x004F7DF0: the rails rebuilt from the buffer.
    void set_packed(const void* data, size_t size);
    void unpack();
    // 0x004F6680: the rails as CreatedRails in `params` (an array of {points: [{pos, HasPost}]}).
    void write_to_structure(script::Struct* params);
    // 0x004F7ED0: the rails read back from `params`' CreatedRails (after clearing).
    void decode_from_struct(script::Struct* params);
    // 0x004F6920: every point added to `nodes` from `*index` on as a RailNode (ncomp_railnode_park, its cluster
    // CreatedRailCluster<rail>, TERRAIN_METALSMOOTH; named from `name` on, one each), linked to the next; `*index` moved
    // on past them.
    void write_nodes(script::Array* nodes, int* index, uint32_t name);
    // 0x004F6510: points with a post put back over the ground at their height (collision off while asking).
    void restore_heights();
    // 0x004F6560: every point's height over the ground kept.
    void store_heights();
    // 0x004F65A0 / 0x004F6600: every segment / post drawn in place.
    void draw_all_segments();
    void draw_all_posts();
    // 0x004F6650: the collision rebuilt, heights kept, everything drawn again.
    void reassemble();
    // 0x004F7230: copy_inside of every rail, into one list (returned).
    [[nodiscard]] EditedRail* copy_rails_inside(float x0, float z0, float x1, float z1) const;
    // 0x004F6BF0 (no this): the free points of the current pool.
    static int free_points();

    // The pools' CPoolable new / delete: 0x004F4990 / 0x004F4A70 make the current pool of points / rails (owned);
    // 0x004F4AF0 / 0x004F4B30 a rail's new / delete; 0x004F4A10 a point's delete.
    static void make_point_pool(int count, const char* name);
    static void make_rail_pool(int count, const char* name);
    static void* new_rail_memory(unsigned int size);
    static void free_rail(void* rail);
    static void free_point(void* point);

    // The fields other code reads and sets.
    [[nodiscard]] EditedRail* rail_groups() const { return rail_groups_; }
    void set_rail_groups(EditedRail* value) { rail_groups_ = value; }
    [[nodiscard]] EditedRail* active_node() const { return active_node_; }
    void set_active_node(EditedRail* value) { active_node_ = value; }

private:
    objects::ObjectModule* input_ptr_;  // +0x18 the input component (0x27D7CD28)
    objects::ObjectModule* view_camera_; // +0x1C the editor camera component (0xAC8946E2)
    EditedRail* rail_groups_;   // +0x20
    EditedRail* active_node_{}; // +0x24 the rail points are added to
    uint32_t mode_sel_{};       // +0x28 SetEditingMode's Mode
    uint32_t dotted_line_name_; // +0x2C the dotted line's clone (Sk5Ed_RAdot_Dynamic / ..._Green)
    render::WorldCell* dotted_line_; // +0x30 its sector
    rail_buffer_format::Bytes packed_{}; // +0x34 the rails packed (rail_buffer_format.h)
    std::array<uint8_t, 2> pad1166{};
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(RailAuthoring, input_ptr_) == 0x18 && offsetof(RailAuthoring, rail_groups_) == 0x20 &&
                          offsetof(RailAuthoring, active_node_) == 0x24 && offsetof(RailAuthoring, mode_sel_) == 0x28 &&
                          offsetof(RailAuthoring, dotted_line_) == 0x30 && offsetof(RailAuthoring, packed_) == 0x34 &&
                          sizeof(RailAuthoring) == 0x1168,
                      "layout must match the original (0x004F7FB0 allocates 0x1168 bytes)");
    }
};

} // namespace park_editing
