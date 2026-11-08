// NOT ORIGINAL - the level editor's scenery groups: the units a click selects beyond one piece (a prop pack's
// placement, a family of pieces named for one), the shadows that follow their casters, and the old inference by touch
// and trick cluster as explicit commands (ours). See level_editor_scenery.h.

#include "editor/level_editor_scenery.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "engine/gfx/render_device.h"
#include "editor/level_editor.h"
#include "base/core/checksums.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_props.h"
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
#include "base/core/checked_index.h"
#include "editor/level_editor_scenery_internal.h"

namespace frontend::level_editor {
// What the source file's anonymous namespace held that its pieces share (level_editor_scenery_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor

namespace frontend::level_editor::scenery {
// What the source file's anonymous namespace held that its pieces share (level_editor_scenery_internal.h): visible
// as it was. (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::level_editor::scenery

namespace frontend::level_editor {
namespace {

constexpr float kTouchMargin = 2.0F; // pieces this close count as touching
constexpr size_t kMinStem = 3;       // a family's stem is at least this long ("Fire_Tender")
constexpr int kMaxFamilyDepth = 8;   // a name chain longer than this is taken as broken

} // namespace
namespace scenery {
namespace {

// A sector of a scene placed as the ones looked at: its box (as loaded), whether it is in the group being made, and -
// worked out when first asked - whether it is see-through and its low-and-flat share.
struct GroupPiece {
    uint32_t checksum{};
    render::Geom* geom{};
    V3 min{}, max{};
    bool in{};
    int see_through_ = -1;      // (-1: not asked yet)
    float level_share_ = -1.0F; // (Picking::get_level_share: structure's low-and-flat rule; -1: not asked yet)
};

bool see_through(GroupPiece& piece)
{
    if (piece.see_through_ < 0) {
        piece.see_through_ = geom_see_through(piece.geom) ? 1 : 0;
    }
    return piece.see_through_ == 1;
}

// (by size only: a camera nowhere near)
constexpr V3 kNowhere = {.x = 1.0e9F, .y = 1.0e9F, .z = 1.0e9F};

bool structure(GroupPiece& piece)
{
    if (piece.level_share_ < 0.0F) {
        piece.level_share_ = geom_level_share(piece.checksum, piece.geom);
    }
    return picking::is_structure(piece.min, piece.max, kNowhere, piece.level_share_);
}

// A scene's shown pieces that can move, placed as `placement` (moved together - a group moved into something doesn't
// take it in, and a piece moved away from its group no longer comes with it).
std::vector<GroupPiece> pieces_placed(render::WorldScene* scene, const Placement& placement)
{
    std::vector<GroupPiece> pieces;
    for_each_sector(scene, [&](uint32_t other, render::WorldCell* sector) {
        render::Geom* geom = geom_of(sector);
        V3 min{};
        V3 max{};
        if (!plain_geom(geom) || !render::device().geom_active(geom) || !geom_box(geom, &min, &max) ||
            !sector_size_movable(min, max, kMaxSectorExtent) || !same_placement(placement_of(other, geom), placement)) {
            return;
        }
        pieces.push_back({.checksum = other, .geom = geom, .min = min, .max = max});
    });
    return pieces;
}

// The group being made: its scene, the pieces placed as its members, the group, whether its members are structure.
struct GroupBuild {
    render::WorldScene* scene{};
    std::vector<GroupPiece> pieces;
    std::vector<uint32_t> group;
    bool clicked_structure_{};
};

// A piece taken into the group (not one in it or one that can't move; a structure only when the members are).
bool take_piece(GroupBuild& build, GroupPiece& piece)
{
    if (piece.in || why_not_movable(piece.checksum)) {
        return false;
    }
    if (!build.clicked_structure_ && structure(piece)) {
        return false;
    }
    piece.in = true;
    build.group.push_back(piece.checksum);
    return true;
}

// The group's box.
struct GroupBox {
    V3 min{};
    V3 max{};
};

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
    std::vector<uint32_t> joined;
    for (const GroupPiece& piece : build.pieces) {
        if (piece.in || !piece_fits(piece.min, piece.max, box.min, box.max)) {
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

// The group grown, three rounds at most, by what touches it inside its box (and those pieces' clusters) - Select
// Touching (the editor's click once did this by itself).
void grow_group(GroupBuild& build, const GroupBox& box)
{
    for (int round = 0; round < 3; ++round) {
        const std::vector<uint32_t> joined = touching_pieces(build, box);
        if (joined.empty()) {
            break;
        }
        for (const uint32_t joining : joined) {
            const uint32_t joining_cluster = cluster_of(joining);
            for (GroupPiece& piece : build.pieces) {
                if (piece.checksum == joining ||
                    ((joining_cluster != 0U) && cluster_of(piece.checksum) == joining_cluster &&
                     piece_fits(piece.min, piece.max, box.min, box.max))) {
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
ShadowCaster caster_by_place(GroupBuild& build, const GroupPiece& piece)
{
    std::vector<std::pair<V3, V3>> others;
    for (GroupPiece& other : build.pieces) {
        if (!other.in && shadows::stands_by(other.min, piece.max) && !see_through(other) && !structure(other)) {
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
// one of its pieces. Not by place for a floor or wall by itself.
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
        } else if (!build.clicked_structure_ && see_through(piece)) {
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

// Each set of `sectors` that share a scene and a placement made into a build (its pieces those placed so, the sectors
// in it) and handed to `fn`; what each build's group became, in order, each once.
template <typename F>
std::vector<uint32_t> by_placement(const std::vector<uint32_t>& sectors, F fn)
{
    std::vector<uint32_t> out;
    std::vector<uint8_t> done(sectors.size(), 0);
    for (size_t i = 0; i < sectors.size(); ++i) {
        if (core::at(done, i) != 0U) {
            continue;
        }
        render::WorldScene* scene = nullptr;
        render::WorldCell* first = find_sector(core::at(sectors, i), &scene);
        if (!first || !plain_geom(geom_of(first))) {
            continue;
        }
        const Placement placement = placement_of(core::at(sectors, i), geom_of(first));
        GroupBuild build{.scene = scene, .pieces = pieces_placed(scene, placement)};
        bool all_structure = true;
        for (size_t j = i; j < sectors.size(); ++j) {
            if (core::at(done, j) != 0U) {
                continue;
            }
            for (GroupPiece& piece : build.pieces) {
                if (piece.checksum == core::at(sectors, j) && !piece.in) {
                    core::at(done, j) = 1;
                    piece.in = true;
                    build.group.push_back(piece.checksum);
                    all_structure = all_structure && structure(piece);
                }
            }
        }
        build.clicked_structure_ = all_structure && !build.group.empty();
        fn(build);
        for (const uint32_t member : build.group) {
            if (std::ranges::find(out, member) == out.end()) {
                out.push_back(member);
            }
        }
    }
    return out;
}

// The pieces placed as `checksum` is, and copies or not as it is: what a unit beyond one piece is made of. `at` its
// index in them (-1: it isn't one - hidden, or too big to move).
struct Kin {
    std::vector<GroupPiece> pieces;
    int at = -1;
};

Kin kin_of(uint32_t checksum)
{
    Kin kin;
    render::WorldScene* scene = nullptr;
    render::WorldCell* sector = find_sector(checksum, &scene);
    if (!sector || !plain_geom(geom_of(sector))) {
        return kin;
    }
    const bool copy = is_clone(checksum);
    for (const GroupPiece& piece : pieces_placed(scene, placement_of(checksum, geom_of(sector)))) {
        if (is_clone(piece.checksum) == copy) {
            if (piece.checksum == checksum) {
                kin.at = static_cast<int>(kin.pieces.size());
            }
            kin.pieces.push_back(piece);
        }
    }
    return kin;
}

// A prop pack's placement among `pieces` (as kin_of gathers them): the pieces cut from the same pack as piece `at`.
std::vector<uint32_t> pack_members(const std::vector<GroupPiece>& pieces, int at, std::string* name)
{
    props::PackOf pack;
    if (at < 0 || !props::pack_of(source_of(core::at(pieces, at).checksum), &pack)) {
        return {};
    }
    std::vector<uint32_t> members;
    for (const GroupPiece& piece : pieces) {
        props::PackOf other;
        if (props::pack_of(source_of(piece.checksum), &other) && other.id == pack.id) {
            members.push_back(piece.checksum);
        }
    }
    *name = pack.piece;
    return members;
}

// Name families among `pieces` (as kin_of gathers them): a piece is part of another when its name is the other's, an
// underscore and more that starts with a letter (Fire_Tender_Ladder of Fire_Tender; Bench_02 isn't of Bench - a number
// is another of the same, not a part), the other's name is at least kMinStem long, neither is structure (a floor, a
// wall), and their boxes touch (kTouchMargin: attached, not just named alike). The longest such name is its parent;
// a family is a root and everything whose chain of parents leads to it. Shadow-named pieces take no part.
class Families {
public:
    explicit Families(std::vector<GroupPiece>& pieces) : pieces_(&pieces), parent_(pieces.size(), -2)
    {
        names_.reserve(pieces.size());
        for (size_t i = 0; i < pieces.size(); ++i) {
            const char* name = get_name_of(source_of(core::at(pieces, i).checksum));
            // (a shadow is never a family's: it follows its caster by the shadows' own rules - add_shadows)
            std::string lower = (name != nullptr) && !shadows::shadow_name(name) ? name : "";
            std::ranges::transform(lower, lower.begin(), [](char letter) { return static_cast<char>(std::tolower(static_cast<unsigned char>(letter))); });
            if (!lower.empty()) {
                by_name_[lower].push_back(static_cast<int>(i));
            }
            names_.push_back(std::move(lower));
        }
    }

    int root_of(int index)
    {
        int at = index;
        for (int depth = 0; depth < kMaxFamilyDepth; ++depth) {
            const int up = parent_of(at);
            if (up < 0) {
                break;
            }
            at = up;
        }
        return at;
    }

    // The family of piece `index` (empty: it has none - alone).
    std::vector<uint32_t> family_of(int index, std::string* name)
    {
        const int root = root_of(index);
        const std::string& stem = core::at(names_, root);
        std::vector<uint32_t> members;
        for (size_t i = 0; i < pieces_->size(); ++i) {
            if (std::cmp_equal(i, root) ||
                (core::at(names_, i).size() > stem.size() && core::at(names_, i).starts_with(stem) && root_of(static_cast<int>(i)) == root)) {
                members.push_back(core::at(*pieces_, i).checksum);
            }
        }
        if (members.size() < 2) {
            return {};
        }
        *name = caption(source_of(core::at(*pieces_, root).checksum));
        return members;
    }

private:
    int parent_of(int index)
    {
        int& cached = core::at(parent_, index);
        if (cached != -2) {
            return cached;
        }
        cached = -1;
        GroupPiece& piece = core::at(*pieces_, index);
        const std::string& name = core::at(names_, index);
        for (size_t cut = name.rfind('_'); cut != std::string::npos && cut >= kMinStem; cut = name.rfind('_', cut - 1)) {
            const unsigned char next = cut + 1 < name.size() ? static_cast<unsigned char>(core::at(name, cut + 1)) : 0;
            const auto found = by_name_.find(name.substr(0, cut));
            if (std::isalpha(next) == 0 || found == by_name_.end()) {
                continue;
            }
            for (const int other : found->second) {
                GroupPiece& candidate = core::at(*pieces_, other);
                if (other != index &&
                    piece_touches(piece.min, piece.max, candidate.min, candidate.max, kTouchMargin) &&
                    !structure(piece) && !structure(candidate)) {
                    cached = other;
                    return cached;
                }
            }
        }
        return cached;
    }

    std::vector<GroupPiece>* pieces_; // (the caller's: it outlives this)
    std::vector<std::string> names_;
    std::unordered_map<std::string, std::vector<int>> by_name_;
    std::vector<int> parent_; // (-2: not worked out yet; -1: none)
};

} // namespace
namespace internal {

std::vector<uint32_t> with_shadows(const std::vector<uint32_t>& sectors)
{
    return by_placement(sectors, [](GroupBuild& build) { add_shadows(build); });
}

std::vector<uint32_t> touching_of(const std::vector<uint32_t>& sectors)
{
    return by_placement(sectors, [](GroupBuild& build) {
        // (structure takes structure: a wall's own trim; a small piece never takes the floor it lies on)
        build.clicked_structure_ = std::ranges::any_of(build.pieces, [](GroupPiece& piece) { return piece.in && structure(piece); });
        grow_group(build, group_box(build));
        add_shadows(build);
    });
}

std::vector<uint32_t> cluster_members_of(const std::vector<uint32_t>& sectors)
{
    return by_placement(sectors, [](GroupBuild& build) {
        std::vector<uint32_t> clusters;
        for (const uint32_t member : build.group) {
            if (const uint32_t cluster = cluster_of(member)) {
                clusters.push_back(cluster);
            }
        }
        for (GroupPiece& piece : build.pieces) {
            const uint32_t cluster = cluster_of(piece.checksum);
            if ((cluster != 0U) && std::ranges::find(clusters, cluster) != clusters.end()) {
                take_piece(build, piece);
            }
        }
        add_shadows(build);
    });
}

std::vector<uint32_t> pack_unit(uint32_t checksum, std::string* name)
{
    const Kin kin = kin_of(checksum);
    return pack_members(kin.pieces, kin.at, name);
}

std::vector<uint32_t> family_unit(uint32_t checksum, std::string* name)
{
    Kin kin = kin_of(checksum);
    if (kin.at < 0) {
        return {};
    }
    Families families(kin.pieces);
    return families.family_of(kin.at, name);
}

std::vector<AutoGroup> find_auto_groups()
{
    std::vector<AutoGroup> found;
    for_each_scene([&](render::WorldScene* scene) {
        // (the scene's pieces by placement and copy - the kin of each)
        struct Bucket {
            Placement placement;
            bool copy{};
            std::vector<GroupPiece> pieces;
        };
        std::vector<Bucket> buckets;
        for_each_sector(scene, [&](uint32_t checksum, render::WorldCell* sector) {
            render::Geom* geom = geom_of(sector);
            V3 min{};
            V3 max{};
            if (!plain_geom(geom) || !render::device().geom_active(geom) || !geom_box(geom, &min, &max) ||
                !sector_size_movable(min, max, kMaxSectorExtent)) {
                return;
            }
            const Placement placement = placement_of(checksum, geom);
            const bool copy = is_clone(checksum);
            auto bucket = std::ranges::find_if(buckets, [&](const Bucket& each) {
                return each.copy == copy && same_placement(each.placement, placement);
            });
            if (bucket == buckets.end()) {
                buckets.push_back({.placement = placement, .copy = copy, .pieces = {}});
                bucket = buckets.end() - 1;
            }
            bucket->pieces.push_back({.checksum = checksum, .geom = geom, .min = min, .max = max});
        });
        for (Bucket& bucket : buckets) {
            std::vector<uint8_t> covered(bucket.pieces.size(), 0);
            Families families(bucket.pieces);
            for (size_t i = 0; i < bucket.pieces.size(); ++i) {
                if (core::at(covered, i) != 0U) {
                    continue;
                }
                AutoGroup group;
                group.sectors = pack_members(bucket.pieces, static_cast<int>(i), &group.name);
                group.pack = !group.sectors.empty();
                if (group.sectors.empty() && families.root_of(static_cast<int>(i)) == static_cast<int>(i)) {
                    group.sectors = families.family_of(static_cast<int>(i), &group.name); // (from its root, once)
                }
                if (group.sectors.size() < 2) {
                    continue;
                }
                for (size_t j = 0; j < bucket.pieces.size(); ++j) {
                    if (std::ranges::find(group.sectors, core::at(bucket.pieces, j).checksum) != group.sectors.end()) {
                        core::at(covered, j) = 1;
                    }
                }
                found.push_back(std::move(group));
            }
        }
    });
    return found;
}

} // namespace internal
} // namespace scenery
} // namespace frontend::level_editor
