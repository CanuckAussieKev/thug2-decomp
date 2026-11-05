// NOT ORIGINAL - the level editor's scenery groups: which pieces of the level belong together (touching boxes, the
// shadows that follow their casters) (ours). See level_editor_scenery.h.

#include "editor/level_editor_scenery.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "engine/gfx/render_device.h"
#include "editor/level_editor.h"
#include "base/core/checksums.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scale.h"
#include "editor/level_editor_shadows.h"
#include "editor/level_editor_shared_coll.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/sector.h"
#include "platform/run_log.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "editor/level_editor_scenery_internal.h"

namespace frontend::level_editor {
namespace {

constexpr float kTouchMargin = 2.0F;     // pieces this close count as touching

} // namespace
namespace scenery {
namespace {
namespace {

// A sector of the clicked one's scene: its box, whether it is in the group, see-through, its low-and-flat share.
struct GroupPiece {
    uint32_t checksum;
    V3 min, max;
    bool in;
    bool see_through_;
    float level_share_; // (Picking::get_level_share: structure's low-and-flat rule)
};

// The group being made: the clicked sector and its scene, the pieces, the group, whether the clicked one is a
// structure.
struct GroupBuild {
    uint32_t checksum{};
    render::WorldScene* scene{};
    std::vector<GroupPiece> pieces;
    std::vector<uint32_t> group;
    bool clicked_structure_{};
};

// (by size only: a camera nowhere near)
constexpr V3 kNowhere = {.x = 1.0e9F, .y = 1.0e9F, .z = 1.0e9F};

// A piece taken into the group (not one in it or one that can't move; a structure only when the clicked one is one).
bool take_piece(GroupBuild& build, GroupPiece& piece)
{
    if (piece.in || why_not_movable(piece.checksum)) {
        return false;
    }
    if (piece.checksum != build.checksum && !build.clicked_structure_ &&
        picking::is_structure(piece.min, piece.max, kNowhere, piece.level_share_)) {
        return false;
    }
    piece.in = true;
    build.group.push_back(piece.checksum);
    return true;
}

// A group's box.
struct GroupBox {
    V3 min{};
    V3 max{};
};

// The group's box.
GroupBox group_box(const GroupBuild& build)
{
    V3 group_min = {};
    V3 group_max = {};
    bool have = false;
    for (const GroupPiece& piece : build.pieces) {
        if (piece.in) {
            grow_box(&group_min, &group_max, have, piece.min, piece.max);
            have = true;
        }
    }
    return {.min = group_min, .max = group_max};
}

// The pieces that fit in the group's box and touch one of its pieces (each round's joiners).
std::vector<uint32_t> touching_pieces(const GroupBuild& build, const GroupBox& box)
{
    const V3 group_min = box.min;
    const V3 group_max = box.max;
    std::vector<uint32_t> joined;
    for (const GroupPiece& piece : build.pieces) {
        if (piece.in || !piece_fits(piece.min, piece.max, group_min, group_max)) {
            continue;
        }
        for (const GroupPiece& member : build.pieces) {
            if (member.in && piece_touches(piece.min, piece.max, member.min, member.max, kTouchMargin)) {
                joined.push_back(piece.checksum);
                break;
            }
        }
    }
    return joined;
}

// A free-standing group grows, three rounds at most, by what touches it inside its box (and those pieces'
// clusters).
void grow_group(GroupBuild& build, const GroupBox& box)
{
    const V3 group_min = box.min;
    const V3 group_max = box.max;
    // (only a free-standing thing grows: not a floor, a wall or a whole building - what stands on or against those isn't
    // part of them)
    const V3 group_size = group_max - group_min;
    const bool grows = std::min({group_size.x, group_size.y, group_size.z}) >= 20.0F &&
                       std::max({group_size.x, group_size.y, group_size.z}) <= 1500.0F;
    for (int round = 0; grows && round < 3; ++round) {
        const std::vector<uint32_t> joined = touching_pieces(build, box);
        if (joined.empty()) {
            break;
        }
        for (const uint32_t joining : joined) {
            const uint32_t joining_cluster = cluster_of(joining);
            for (GroupPiece& piece : build.pieces) {
                if (piece.checksum == joining ||
                    ((joining_cluster != 0U) && cluster_of(piece.checksum) == joining_cluster &&
                     piece_fits(piece.min, piece.max, group_min, group_max))) {
                    if (take_piece(build, piece) && debug_enabled()) {
                        get_log("level editor:   joins: %s (%.0f x %.0f x %.0f)", caption(piece.checksum).c_str(),
                                piece.max.x - piece.min.x, piece.max.y - piece.min.y, piece.max.z - piece.min.z);
                    }
                }
            }
        }
    }
}

// A copy's source (itself when it isn't one).
uint32_t source_of(uint32_t of)
{
    for (const Replicate& clone : get_clones()) {
        if (clone.checksum == of) {
            return clone.source;
        }
    }
    return of;
}

// The shadow piece's caster in the group, and how it was found (nullptr: none).
struct ShadowCaster {
    uint32_t caster{};
    const char* how{};
};

// By name: a group piece that is its owner (a copy's shadow: a copy of its owner, and by it - copies of one piece
// share names).
ShadowCaster caster_by_name(const GroupBuild& build, const GroupPiece& piece, uint32_t owner)
{
    const bool clone = source_of(piece.checksum) != piece.checksum;
    for (const GroupPiece& member : build.pieces) {
        if (member.in && source_of(member.checksum) == owner && (member.checksum != owner) == clone &&
            (!clone || shadows::get_beside(piece.min, piece.max, member.min, member.max))) {
            return {.caster = member.checksum, .how = "by name"};
        }
    }
    return {};
}

// By place: under one of the group's pieces - not a shadow taken here - and nearer to it than to anything else
// standing there.
ShadowCaster caster_by_place(const GroupBuild& build, const GroupPiece& piece)
{
    std::vector<std::pair<V3, V3>> others;
    for (const GroupPiece& other : build.pieces) {
        if (!other.in && !other.see_through_ && shadows::stands_by(other.min, piece.max) &&
            !picking::is_structure(other.min, other.max, kNowhere, other.level_share_)) {
            others.emplace_back(other.min, other.max);
        }
    }
    for (const GroupPiece& member : build.pieces) {
        if (member.in && !shadow_of().contains(member.checksum) &&
            shadows::falls_under(piece.min, piece.max, member.min, member.max) &&
            shadows::nearest_to_group({.shadow_min_ = piece.min,
                                       .shadow_max_ = piece.max,
                                       .group_min_ = member.min,
                                       .group_max_ = member.max},
                                      others)) {
            return {.caster = member.checksum, .how = "by place"};
        }
    }
    return {};
}

// Its shadows (level_editor_shadows.h): by name, then - for shadow-named pieces nobody owns by name - by place, under
// one of its pieces. Not by place for a floor or wall clicked by itself.
void add_shadows(GroupBuild& build)
{
    for (GroupPiece& piece : build.pieces) {
        if (piece.in || why_not_movable(piece.checksum)) {
            continue;
        }
        const uint32_t source = source_of(piece.checksum);
        const char* name = get_name_of(source);
        if (!name || !shadows::shadow_name(name)) {
            continue;
        }
        uint32_t owner = 0;
        for (const std::string& candidate : shadows::owner_names(name)) {
            const uint32_t crc = checksums::hash_from_text(candidate.c_str());
            if (crc != source && lookup_sector(build.scene, crc)) {
                owner = crc;
                break;
            }
        }
        ShadowCaster found;
        if (owner != 0U) {
            found = caster_by_name(build, piece, owner);
        } else if (!build.clicked_structure_ && piece.see_through_) {
            found = caster_by_place(build, piece);
        }
        const uint32_t caster = found.caster;
        const char* how = found.how;
        if (!how) {
            continue;
        }
        piece.in = true;
        build.group.push_back(piece.checksum);
        shadow_of()[piece.checksum] = caster;
        if (debug_enabled()) {
            get_log("level editor:   shadow %s: %s (%.0f x %.0f x %.0f)%s%s", how, caption(piece.checksum).c_str(),
                    piece.max.x - piece.min.x, piece.max.y - piece.min.y, piece.max.z - piece.min.z,
                    (caster != 0U) ? " of " : "", (caster != 0U) ? caption(caster).c_str() : "");
        }
    }
}

} // namespace
} // namespace
namespace internal {

// The group a click on a sector selects: the sector, the rest of its cluster, and the movable pieces built onto them
// (piece_fits / piece_touches, three rounds - each with its cluster). Decided on the level's own layout (the boxes as
// loaded) and only among pieces the editor has moved together (the same placement): a group moved into something
// doesn't take it in, and a piece moved away from its group no longer brings the group along. Hidden pieces aren't
// taken, nor structure (Picking::is_structure by size - a floor, a wall) unless the clicked piece is structure itself:
// a level's own clusters can join a small piece to the floor it lies on (TR's SkWare_Main_12, 360 x 60 x 924, and the
// 3000 x 99 x 2431 floor SkWare_Main_9), and a click on the small piece mustn't take the floor. Then the group's shadows
// (level_editor_shadows.h).
std::vector<uint32_t> group_of(uint32_t checksum)
{
    GroupBuild build{.checksum = checksum};
    const std::vector<uint32_t>& group = build.group;
    render::WorldCell* clicked = find_sector(checksum, &build.scene);
    if (why_not_movable(checksum) || !clicked) {
        return group;
    }
    Placement placement = placement_of(checksum, geom_of(clicked));
    std::vector<GroupPiece>& pieces =
        build.pieces; // (the scene's shown sectors small enough to move, moved as the clicked one)
    for_each_sector(build.scene, [&](uint32_t other, render::WorldCell* sector) {
        render::Geom* geom = geom_of(sector);
        V3 min{};
        V3 max{};
        if (!plain_geom(geom) || !render::device().geom_active(geom) || !geom_box(geom, &min, &max) ||
            !sector_size_movable(min, max, kMaxSectorExtent) || !same_placement(placement_of(other, geom), placement)) {
            return;
        }
        pieces.push_back({.checksum = other,
                          .min = min,
                          .max = max,
                          .in = false,
                          .see_through_ = geom_see_through(geom),
                          .level_share_ = geom_level_share(other, geom)});
    });
    for (const GroupPiece& piece : pieces) {
        if (piece.checksum == checksum) {
            build.clicked_structure_ = picking::is_structure(piece.min, piece.max, kNowhere, piece.level_share_);
        }
    }
    const uint32_t cluster = cluster_of(checksum);
    for (GroupPiece& piece : pieces) {
        if (piece.checksum == checksum || ((cluster != 0U) && cluster_of(piece.checksum) == cluster)) {
            take_piece(build, piece);
        }
    }
    if (group.empty()) {
        return group;
    }
    grow_group(build, group_box(build));
    add_shadows(build);
    return std::move(build.group);
}

} // namespace internal
} // namespace scenery
} // namespace frontend::level_editor
